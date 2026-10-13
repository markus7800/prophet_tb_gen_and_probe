#ifndef COMPRESSED_TB_H_INCLUDED
#define COMPRESSED_TB_H_INCLUDED

#ifdef OMP
#include <omp.h>
#endif
#include <iostream>
#include <cassert>
#include <zstd.h>
#include <math.h>
#include <unistd.h> // close
#include <fcntl.h> // open
#include <sys/mman.h> // mmap
#include <sys/stat.h> // file size
#include <cstdint>
#include <stdio.h>


#define COMP_EXT ".bz"
/*
.bz FORMAT (version 2):

HEADER 7 x UINT64 = 56 bytes
magic UINT64 = "PROPHTB2"
version UINT64 = 2
header_size UINT64 = number of bytes before BLOCK_SIZES (56, later versions may append fields)
checksum UINT64 (see compute_checksum)
num_pos UINT64
block_size UINT64
flags UINT64 (FLAG_RANK_ENCODED, FLAG_ESCAPE)
if flags & FLAG_RANK_ENCODED:
  dict_size UINT64 (number of values in the dictionary, at most 256, at most 255 if FLAG_ESCAPE)
  dict_size * INT16 dictionary values, zero-padded to a multiple of 8 bytes

BLOCK_SIZES
if block_size > 65535:
  n_blocks * UINT32
else
  n_blocks * UINT16
where  n_blocks = ceil((double) num_pos / block_size);

COMPRESSED DATA
individual compressed blocks concatenated at byte level
without FLAG_RANK_ENCODED, a block is the zstd-compressed int16 values of the block
with FLAG_RANK_ENCODED, a block is the zstd-compressed payload:
  n UINT8 codes (n = number of entries in the block), code c < dict_size stands for dictionary value c
  if FLAG_ESCAPE, code dict_size is the escape code: the actual INT16 values of all escaped entries
  follow the n codes in order of occurrence
The dictionary is global for the table and sorted by decreasing frequency.

Legacy version 1 (read-only) has no magic and a 3 x UINT64 header: checksum, num_pos, block_size.
Its checksum is the XOR of (ix << 10) | abs(value), see compute_checksum_v1.
*/

#define COMP_MAGIC 0x32425448504F5250ULL // bytes "PROPHTB2" read as little-endian uint64
#define FLAG_RANK_ENCODED 1
#define FLAG_ESCAPE 2
// maximal number of codes (dictionary values + escape code); lower values can be set at compile time to test the escape path
#ifndef EGTB_MAX_DICT
#define EGTB_MAX_DICT 256
#endif
static_assert(2 <= EGTB_MAX_DICT && EGTB_MAX_DICT <= 256, "EGTB_MAX_DICT must be in [2, 256]");
#define COMP_VERSION 2
#define COMP_V1_HEADER_SIZE (3 * sizeof(uint64_t))
#define COMP_V2_HEADER_SIZE (7 * sizeof(uint64_t))

struct CompressedHeader {
  uint64_t version;
  uint64_t header_size; // bytes before the block size table
  uint64_t checksum;
  uint64_t num_pos;
  uint64_t block_size;
  uint64_t flags;
};

// checksum contribution of one entry: position and value are mixed, so that sign flips, swapped values and
// values in wrong positions are detected; the checksum is the sum (mod 2^64) over all entries
inline uint64_t checksum_term(uint64_t ix, int16_t val) {
  uint64_t z = (ix << 16) | (uint16_t) val;
  z += 0x9e3779b97f4a7c15ULL;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}
inline uint64_t checksum_term_v1(uint64_t ix, int16_t val) {
  return (ix << 10) | ((uint64_t) abs(val));
}

uint64_t compute_checksum(const int16_t* TB, uint64_t num_pos, [[maybe_unused]] int nthreads);
uint64_t compute_checksum_v1(const int16_t* TB, uint64_t num_pos, [[maybe_unused]] int nthreads);
// checksum of the given file format version
uint64_t compute_checksum_version(uint64_t version, const int16_t* TB, uint64_t num_pos, int nthreads);

struct ValueDict {
  uint64_t size; // number of dictionary values
  bool escape; // if true, code size is the escape code
  int16_t values[256];
};

// builds the dictionary of the most frequent values of TB
void build_value_dict(const int16_t* TB, uint64_t num_pos, int nthreads, ValueDict& dict);
// encodes n values into payload (n codes followed by escaped int16 values), returns payload size in bytes
// code_of maps (uint16_t) value to its code, or to -1 if the value is escaped; payload must hold 3*n bytes
uint64_t encode_block(const int16_t* values, uint64_t n, const int16_t* code_of, uint8_t escape_code, uint8_t* payload);
// decodes a payload of n entries, returns false if the payload is inconsistent
bool decode_block(const uint8_t* payload, uint64_t payload_size, uint64_t n, const ValueDict& dict, int16_t* values);

uint64_t block_compress_TB(int16_t* TB, uint64_t num_pos, int nthreads, int compression_level, uint64_t block_size, std::string compressed_filename, bool write, bool verbose);

// parses a header of version 1 or 2 from the first n bytes of a file, returns false if invalid
bool parse_compressed_header(const uint8_t* data, uint64_t n, CompressedHeader& header);
// reads header of compressed file, returns false if file cannot be read or is invalid
bool read_compressed_header(std::string compressed_filename, CompressedHeader& header);
bool read_compressed_header(std::string compressed_filename, uint64_t& checksum, uint64_t& num_pos, uint64_t& block_size);
// parses the value dictionary that follows the fixed header (header_size bytes of data have to be available)
// for files without FLAG_RANK_ENCODED the dictionary is empty
bool parse_value_dict(const uint8_t* data, uint64_t n, const CompressedHeader& header, ValueDict& dict);
// checks that compressed file is not truncated, i.e. header, block sizes and all blocks are present
bool is_complete_compressed_file(std::string compressed_filename);
// decompresses source and block-compresses it with block_size to target (source == target is allowed)
// verifies that target is complete and matches source, exits otherwise
void recompress_TB(std::string source_filename, std::string target_filename, int nthreads, int compression_level, uint64_t block_size, bool verbose);

struct DecompressCtx {
  const void* ctb; // CompressedTB whose block is cached (a DecompressCtx may be shared across CompressedTBs)
  uint64_t uncompressed_block_ix;
  uint64_t buf_size;
  int16_t* uncompressed_buf;
  uint8_t* payload_buf; // 3 * buf_size bytes: codes + escaped values of a rank encoded block
  ZSTD_DCtx* decompressor;
  uint64_t probe_count;
  DecompressCtx(uint64_t buf_size_ = 2097152) {
    ctb = nullptr;
    uncompressed_block_ix = UINT64_MAX;
    buf_size = buf_size_;
    uncompressed_buf = (int16_t*) malloc(buf_size * sizeof(int16_t));
    payload_buf = (uint8_t*) malloc(3 * buf_size);
    decompressor = ZSTD_createDCtx();
    probe_count = 0;
  }
  ~DecompressCtx() {
    free(uncompressed_buf);
    free(payload_buf);
    ZSTD_freeDCtx(decompressor);
  }
};

// store offset of every OFFSET_STRIDE-th block, the others are computed from the block sizes
#define OFFSET_STRIDE 32

struct CompressedTB {
  int egtb_ix;
  std::string compressed_filename;
  uint64_t compressed_filesize;
  uint64_t version;
  uint64_t flags;
  uint64_t checksum;
  uint64_t num_pos;
  uint64_t block_size;
  uint64_t n_blocks;
  bool large_blocks;
  uint8_t* block_sizes_ptr; // points into mmaped file, UINT32 if large_blocks else UINT16
  uint64_t* sparse_block_offsets; // offset of every OFFSET_STRIDE-th block
  uint8_t* map_ptr;
  uint8_t* compressed_blocks;
  DecompressCtx* decompress_ctx;
  ValueDict dict; // only used if flags & FLAG_RANK_ENCODED

  CompressedTB(int egtb_ix_, std::string filename) {
    // std::cout << "CompressedEGTB " << this << ": " << filename << std::endl;
    this->egtb_ix = egtb_ix_;
    this->compressed_filename = filename;

    struct stat st;
    stat(this->compressed_filename.c_str(), &st);
    this->compressed_filesize = st.st_size;
    int fd = open(this->compressed_filename.c_str(), O_RDONLY);
    if (fd == -1) {
        printf("Could not open file %s.\n", this->compressed_filename.c_str());
        exit(EXIT_FAILURE);
    }
    this->map_ptr = (uint8_t*) mmap(0, this->compressed_filesize, PROT_READ, MAP_SHARED, fd, 0);
    if (this->map_ptr == MAP_FAILED) {
        close(fd);
        perror("Error mmapping the file");
        exit(EXIT_FAILURE);
    }
    close(fd);

    CompressedHeader header;
    if (!parse_compressed_header(this->map_ptr, this->compressed_filesize, header)) {
        printf("Invalid header in file %s.\n", this->compressed_filename.c_str());
        exit(EXIT_FAILURE);
    }
    uint64_t headersize = header.header_size;
    if (headersize > this->compressed_filesize || !parse_value_dict(this->map_ptr, headersize, header, this->dict)) {
        printf("Invalid header in file %s.\n", this->compressed_filename.c_str());
        exit(EXIT_FAILURE);
    }
    this->version = header.version;
    this->flags = header.flags;
    this->checksum = header.checksum;
    this->num_pos = header.num_pos;
    this->block_size = header.block_size;
    this->n_blocks = ceil((double) num_pos / block_size);
    
    this->large_blocks = block_size > uint64_t(UINT16_MAX);
    this->block_sizes_ptr = &this->map_ptr[headersize];
    this->compressed_blocks = &this->map_ptr[headersize + this->n_blocks * (large_blocks ? sizeof(uint32_t) : sizeof(uint16_t))];

    uint64_t n_sparse = n_blocks / OFFSET_STRIDE + (n_blocks % OFFSET_STRIDE != 0);
    this->sparse_block_offsets = (uint64_t*) malloc(n_sparse * sizeof(uint64_t));
    uint64_t offset = 0;
    for (uint64_t block_ix = 0; block_ix < this->n_blocks; block_ix++) {
      if (block_ix % OFFSET_STRIDE == 0) this->sparse_block_offsets[block_ix / OFFSET_STRIDE] = offset;
      offset += this->get_block_size(block_ix);
    }

    decompress_ctx = nullptr;
    // std::cout << "CompressedEGTB" << this->egtb_id << ": filesize=" << this->filesize << ", num_pos=" << this->num_pos << ", block_size=" << this->block_size << ", n_blocks=" << this->n_blocks << std::endl;
  }

  ~CompressedTB() {
    // std::cout << "~CompressedEGTB " << this << ": " << this->compressed_filename << std::endl;
    int unmap = munmap(this->map_ptr, this->compressed_filesize);
    if (unmap == -1) {
        perror(("Error munmapping the file " + this->compressed_filename).c_str());
        exit(EXIT_FAILURE);
    }
    free(this->sparse_block_offsets);
    if (decompress_ctx != nullptr) delete decompress_ctx;
  }

  // size of compressed block in bytes
  inline uint64_t get_block_size(uint64_t block_ix) const {
    return large_blocks ? ((uint32_t*) block_sizes_ptr)[block_ix] : ((uint16_t*) block_sizes_ptr)[block_ix];
  }

  // offset of compressed block in compressed_blocks
  inline uint64_t get_block_offset(uint64_t block_ix) const {
    uint64_t offset = sparse_block_offsets[block_ix / OFFSET_STRIDE];
    for (uint64_t i = block_ix - block_ix % OFFSET_STRIDE; i < block_ix; i++) offset += get_block_size(i);
    return offset;
  }

  // not thread-safe
  inline int16_t get_value(uint64_t ix) {

    if (decompress_ctx == nullptr)
      decompress_ctx = new DecompressCtx(block_size);

    return get_value_dctx(ix, decompress_ctx);
  }
  
  // thread-safe
  int16_t get_value_dctx(uint64_t ix, DecompressCtx* dctx) {
    assert (dctx->buf_size >= block_size);
    dctx->probe_count++;

    uint64_t block_ix = ix / block_size;
    uint64_t ix_in_block = ix % block_size;

    if (dctx->ctb != this || dctx->uncompressed_block_ix != block_ix) {
      dctx->ctb = this; // DecompressCtx may be shared across CompressedTBs
      dctx->uncompressed_block_ix = block_ix;
      if (!decompress_block(block_ix, dctx, dctx->uncompressed_buf)) {
        std::cerr << "Corrupt block " << block_ix << " in " << compressed_filename << std::endl;
        exit(EXIT_FAILURE);
      }
    }
    return dctx->uncompressed_buf[ix_in_block];
  }

  // number of entries in block block_ix
  inline uint64_t get_block_npos(uint64_t block_ix) const {
    return std::min(block_size, num_pos - block_ix * block_size);
  }

  // decompresses all values of block block_ix into out (at least get_block_npos(block_ix) entries),
  // returns false if the block is corrupt; dctx->uncompressed_buf is not used, but may be passed as out
  bool decompress_block(uint64_t block_ix, DecompressCtx* dctx, int16_t* out) const {
    assert (dctx->buf_size >= block_size);
    uint64_t n = get_block_npos(block_ix);
    const uint8_t* src = &compressed_blocks[get_block_offset(block_ix)];
    uint64_t src_size = get_block_size(block_ix);
    if (flags & FLAG_RANK_ENCODED) {
      size_t res = ZSTD_decompressDCtx(dctx->decompressor, dctx->payload_buf, 3 * n, src, src_size);
      return !ZSTD_isError(res) && decode_block(dctx->payload_buf, res, n, dict, out);
    } else {
      size_t res = ZSTD_decompressDCtx(dctx->decompressor, out, n * sizeof(int16_t), src, src_size);
      return !ZSTD_isError(res) && res == n * sizeof(int16_t);
    }
  }


  void decompress_to_array(int nthreads, int16_t* TB) {
    #pragma omp parallel num_threads(nthreads)
    {
      DecompressCtx* dctx = new DecompressCtx(block_size);

      #pragma omp for schedule(static)
      for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
        uint64_t block_ix = start_ix / block_size;
        if (!decompress_block(block_ix, dctx, &TB[start_ix])) {
          std::cerr << "Corrupt block " << block_ix << " in " << compressed_filename << std::endl;
          exit(EXIT_FAILURE);
        }
      }

      #pragma omp critical
      {
        delete dctx;
      }
    }

    if (!verify_checksum(TB, nthreads)) {
      std::cerr << "Checksum mismatch after decompressing " << compressed_filename << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  // checksum of a decompressed table, computed with the checksum of this file's version
  bool verify_checksum(const int16_t* TB, int nthreads) const {
    return compute_checksum_version(version, TB, num_pos, nthreads) == checksum;
  }

  void decompress_to_file(std::string filename) {
    FILE* f = fopen(filename.c_str(), "wb");
    if (f == NULL) {
        std::cerr << "Error opening file: " << filename << std::endl;
        exit(1);
    }
    if (decompress_ctx == nullptr)
      decompress_ctx = new DecompressCtx(block_size);
    assert (decompress_ctx->buf_size >= block_size);
    for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
        uint64_t block_ix = start_ix / block_size;
        if (!decompress_block(block_ix, decompress_ctx, decompress_ctx->uncompressed_buf)) {
          std::cerr << "Corrupt block " << block_ix << " in " << compressed_filename << std::endl;
          exit(EXIT_FAILURE);
        }
        decompress_ctx->uncompressed_block_ix = UINT64_MAX; // buffer content no longer matches cached block
        fwrite(decompress_ctx->uncompressed_buf, sizeof(int16_t), get_block_npos(block_ix), f);
    }
    fclose(f);
  }

  bool check_integrity([[maybe_unused]] int nthreads);
};

#endif