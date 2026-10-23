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
.bz FORMAT:

HEADER 3 x UINT64 = 24 bytes
checksum UINT64
num_pos UINT64
block_size UINT64

BLOCK_SIZES
if block_size > 65535:
  n_blocks * UINT32
else
  n_blocks * UINT16
where  n_blocks = ceil((double) num_pos / block_size);

COMPRESSED DATA
individual compressed blocks concatenated at byte level
*/ 


uint64_t compute_checksum(int16_t* TB, uint64_t num_pos, [[maybe_unused]] int nthreads);
uint64_t block_compress_TB(int16_t* TB, uint64_t num_pos, int nthreads, int compression_level, uint64_t block_size, std::string compressed_filename, bool write, bool verbose);

// reads header of compressed file, returns false if file cannot be read or is too small
bool read_compressed_header(std::string compressed_filename, uint64_t& checksum, uint64_t& num_pos, uint64_t& block_size);
// checks that compressed file is not truncated, i.e. header, block sizes and all blocks are present
bool is_complete_compressed_file(std::string compressed_filename);
// decompresses source and block-compresses it with block_size to target (source == target is allowed)
// verifies that target is complete and matches source, exits otherwise
void recompress_TB(std::string source_filename, std::string target_filename, int nthreads, int compression_level, uint64_t block_size, bool verbose);

struct DecompressCtx {
  int egtb_ix;
  uint64_t uncompressed_block_ix;
  uint64_t buf_size;
  int16_t* uncompressed_buf;
  ZSTD_DCtx* decompressor;
  uint64_t probe_count;
  DecompressCtx(uint64_t buf_size_ = 2097152) {
    egtb_ix = 1001;
    uncompressed_block_ix = UINT64_MAX;
    buf_size = buf_size_;
    uncompressed_buf = (int16_t*) malloc(buf_size * sizeof(int16_t));
    decompressor = ZSTD_createDCtx();
    probe_count = 0;
  }
  ~DecompressCtx() {
    free(uncompressed_buf);
    ZSTD_freeDCtx(decompressor);
  }
};

// store offset of every OFFSET_STRIDE-th block, the others are computed from the block sizes
#define OFFSET_STRIDE 32

struct CompressedTB {
  int egtb_ix;
  std::string compressed_filename;
  uint64_t compressed_filesize;
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

    int headersize = 3*sizeof(uint64_t);
    this->checksum = ((uint64_t*) this->map_ptr)[0];
    this->num_pos = ((uint64_t*) this->map_ptr)[1];
    this->block_size = ((uint64_t*) this->map_ptr)[2];
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

    if (dctx->egtb_ix != egtb_ix || dctx->uncompressed_block_ix != block_ix) {
      dctx->egtb_ix = egtb_ix; // DecompressCtx may be shared across CompressTB
      dctx->uncompressed_block_ix = block_ix;
      uint64_t offset = get_block_offset(block_ix);
      uint64_t size = std::min(block_size, num_pos - block_ix*block_size) * sizeof(int16_t);
      size_t res = ZSTD_decompressDCtx(dctx->decompressor,
        dctx->uncompressed_buf, size,
        &compressed_blocks[offset], get_block_size(block_ix));
      assert (!ZSTD_isError(res));
    }
    return dctx->uncompressed_buf[ix_in_block];
  }
  

  void decompress_to_array(int nthreads, int16_t* TB) {
    #pragma omp parallel num_threads(nthreads)
    {
      DecompressCtx* dctx = new DecompressCtx(block_size);

      #pragma omp for schedule(static)
      for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
        uint64_t block_ix = start_ix / block_size;
        uint64_t offset = get_block_offset(block_ix);
        uint64_t size = std::min(block_size, num_pos - start_ix) * sizeof(int16_t);
        size_t res = ZSTD_decompressDCtx(dctx->decompressor,
          &TB[start_ix], size,
          &compressed_blocks[offset], get_block_size(block_ix));
        assert (!ZSTD_isError(res));
      }

      #pragma omp critical
      {
        delete dctx;
      }
    }

    assert (compute_checksum(TB, num_pos, nthreads) == checksum);
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
        uint64_t offset = get_block_offset(block_ix);
        uint64_t size = std::min(block_size, num_pos - start_ix) * sizeof(int16_t);
        size_t res = ZSTD_decompressDCtx(decompress_ctx->decompressor,
            decompress_ctx->uncompressed_buf, size,
            &compressed_blocks[offset], get_block_size(block_ix));
        assert (!ZSTD_isError(res));
        fwrite(decompress_ctx->uncompressed_buf, sizeof(uint8_t), size, f);
    }
    fclose(f);
  }

  bool check_integrity([[maybe_unused]] int nthreads);
};

#endif