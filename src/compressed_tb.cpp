
#include "compressed_tb.h"
#include <vector>
#include <cstring>
#include <algorithm>

uint64_t compute_checksum(const int16_t* TB, uint64_t num_pos, [[maybe_unused]] int nthreads) {
  uint64_t s = 0;
  #pragma omp parallel for num_threads(nthreads) schedule(static) reduction(+:s)
  for (uint64_t ix = 0; ix < num_pos; ix++) {
    s += checksum_term(ix, TB[ix]);
  }
  return s;
}

uint64_t compute_checksum_v1(const int16_t* TB, uint64_t num_pos, [[maybe_unused]] int nthreads) {
  uint64_t s = 0;
  #pragma omp parallel for num_threads(nthreads) schedule(static) reduction(^:s)
  for (uint64_t ix = 0; ix < num_pos; ix++) {
    s ^= checksum_term_v1(ix, TB[ix]);
  }
  return s;
}

uint64_t compute_checksum_version(uint64_t version, const int16_t* TB, uint64_t num_pos, int nthreads) {
  return version == 1 ? compute_checksum_v1(TB, num_pos, nthreads) : compute_checksum(TB, num_pos, nthreads);
}

bool CompressedTB::check_integrity([[maybe_unused]] int nthreads) {
  uint64_t s = 0;

  #pragma omp parallel num_threads(nthreads)
  {
    DecompressCtx* dctx = new DecompressCtx(block_size);
    uint64_t local_s = 0;

    #pragma omp for schedule(static)
    for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
      uint64_t block_ix = start_ix / block_size;
      uint64_t offset = get_block_offset(block_ix);
      uint64_t block_npos = std::min(block_size, num_pos - start_ix);
      size_t res = ZSTD_decompressDCtx(dctx->decompressor,
        dctx->uncompressed_buf, block_npos * sizeof(int16_t),
        &compressed_blocks[offset], get_block_size(block_ix));
      assert (!ZSTD_isError(res));

      if (version == 1) {
        for (uint64_t ix = start_ix; ix < start_ix + block_npos; ix++) {
          local_s ^= checksum_term_v1(ix, dctx->uncompressed_buf[ix - start_ix]);
        }
      } else {
        for (uint64_t ix = start_ix; ix < start_ix + block_npos; ix++) {
          local_s += checksum_term(ix, dctx->uncompressed_buf[ix - start_ix]);
        }
      }
    }

    #pragma omp critical
    {
      if (version == 1) s ^= local_s; else s += local_s;
      delete dctx;
    }
  }

  return (s == checksum);
}

uint64_t block_compress_TB(int16_t* TB, uint64_t num_pos, int nthreads, int compression_level, uint64_t block_size, std::string compressed_filename, bool write, bool verbose) {
  uint64_t tb_size_bytes = num_pos * sizeof(int16_t);
  uint64_t checksum = compute_checksum(TB, num_pos, nthreads);

  uint64_t n_blocks = ceil((double) num_pos / block_size);
  uint64_t* block_sizes = (uint64_t*) malloc(n_blocks * sizeof(uint64_t));  

  bool large_blocks = block_size > uint64_t(UINT16_MAX);
  uint64_t max_compressed_block_size = large_blocks ? UINT32_MAX : UINT16_MAX;
  uint64_t final_size = 0;

  // compressed blocks are stored in place in TB, blocks that do not fit (e.g. a tiny last block) are stored here
  // kept for safety but very unlikely
  std::vector<std::pair<uint64_t, std::vector<uint8_t>>> overflow_blocks;

  #pragma omp parallel num_threads(nthreads)
  {
    // thread-private allocations
    std::vector<uint8_t> compressed(ZSTD_compressBound(block_size * sizeof(int16_t)));


    #pragma omp for schedule(static) reduction(+:final_size)
    for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
      uint64_t size = std::min(block_size, num_pos - start_ix) * sizeof(int16_t);
      uint64_t block_ix = start_ix / block_size;

      uint64_t compressed_size = ZSTD_compress(compressed.data(), compressed.size(),  (uint8_t*) &TB[start_ix], size, compression_level);

      if (ZSTD_isError(compressed_size) || compressed_size == 0) {
        std::cerr << "Compression of block " << block_ix << " failed: " << ZSTD_getErrorName(compressed_size) << std::endl;
        exit(1);
      }
      if (compressed_size > max_compressed_block_size) {
        std::cerr << "Compressed size " << compressed_size << " of block " << block_ix << " does not fit into the block size table (max " << max_compressed_block_size << "). ";
        std::cerr << "Use a block size of at most " << UINT16_MAX / 2 << " or larger than " << UINT16_MAX << "." << std::endl;
        exit(1);
      }
      final_size += compressed_size;

      block_sizes[block_ix] = compressed_size;
      if (compressed_size <= size) {
        std::memcpy(&TB[start_ix], compressed.data(), compressed_size);
      } else {
        #pragma omp critical
        overflow_blocks.emplace_back(block_ix, std::vector<uint8_t>(compressed.begin(), compressed.begin() + compressed_size));
      }
    }

  }
  std::sort(overflow_blocks.begin(), overflow_blocks.end());

  final_size += large_blocks ? sizeof(uint32_t) * n_blocks : sizeof(uint16_t) * n_blocks;
  final_size += COMP_V2_HEADER_SIZE;

  if (write) {
    FILE* f = fopen((compressed_filename + ".tmp").c_str(), "wb");
    if (f == NULL) {
      std::cerr << "Error opening file: " << compressed_filename << std::endl;
      exit(1);
    }
    uint64_t header[7] = {COMP_MAGIC, COMP_VERSION, COMP_V2_HEADER_SIZE, checksum, num_pos, block_size, 0};
    static_assert(sizeof(header) == COMP_V2_HEADER_SIZE);
    fwrite(header, sizeof(uint64_t), 7, f);
    if (large_blocks) {
      std::vector<uint32_t> write_block_sizes(block_sizes, block_sizes + n_blocks);
      fwrite(write_block_sizes.data(), sizeof(uint32_t), n_blocks, f);
    } else {
      std::vector<uint16_t> write_block_sizes(block_sizes, block_sizes + n_blocks);
      fwrite(write_block_sizes.data(), sizeof(uint16_t), n_blocks, f);
    }

    auto overflow_block = overflow_blocks.begin();
    for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
      uint64_t block_ix = start_ix / block_size;
      if (overflow_block != overflow_blocks.end() && overflow_block->first == block_ix) {
        fwrite(overflow_block->second.data(), sizeof(uint8_t), block_sizes[block_ix], f);
        overflow_block++;
      } else {
        fwrite(&TB[start_ix], sizeof(uint8_t), block_sizes[block_ix], f);
      }
      assert(block_ix * block_size == start_ix);
    }
    fclose(f);
    system(("mv " + compressed_filename + ".tmp " + compressed_filename).c_str());
    if (verbose) std::cout << "Wrote to " << compressed_filename << std::endl;
  }
  
  if (verbose) std::cout << "Compressed final size: " << final_size << " ratio: " << (double) final_size / tb_size_bytes << std::endl;


  free(block_sizes);
  return final_size;
}


bool parse_compressed_header(const uint8_t* data, uint64_t n, CompressedHeader& header) {
  uint64_t fields[7];
  if (n < sizeof(uint64_t)) return false;
  memcpy(fields, data, sizeof(uint64_t));
  if (fields[0] == COMP_MAGIC) {
    if (n < COMP_V2_HEADER_SIZE) return false; // truncated v2 header
    memcpy(fields, data, COMP_V2_HEADER_SIZE);
    header.version = fields[1];
    header.header_size = fields[2];
    header.checksum = fields[3];
    header.num_pos = fields[4];
    header.block_size = fields[5];
    header.flags = fields[6];
    return header.version == COMP_VERSION && header.header_size >= COMP_V2_HEADER_SIZE && header.block_size > 0;
  } else if (n >= COMP_V1_HEADER_SIZE) {
    memcpy(fields, data, COMP_V1_HEADER_SIZE);
    header.version = 1;
    header.header_size = COMP_V1_HEADER_SIZE;
    header.checksum = fields[0];
    header.num_pos = fields[1];
    header.block_size = fields[2];
    header.flags = 0;
    return header.block_size > 0;
  }
  return false;
}

bool read_compressed_header(std::string compressed_filename, CompressedHeader& header) {
  FILE* f = fopen(compressed_filename.c_str(), "rb");
  if (f == NULL) return false;
  uint8_t data[COMP_V2_HEADER_SIZE];
  size_t n = fread(data, 1, COMP_V2_HEADER_SIZE, f);
  fclose(f);
  return parse_compressed_header(data, n, header);
}

bool read_compressed_header(std::string compressed_filename, uint64_t& checksum, uint64_t& num_pos, uint64_t& block_size) {
  CompressedHeader header;
  if (!read_compressed_header(compressed_filename, header)) return false;
  checksum = header.checksum;
  num_pos = header.num_pos;
  block_size = header.block_size;
  return true;
}

bool is_complete_compressed_file(std::string compressed_filename) {
  CompressedHeader header;
  if (!read_compressed_header(compressed_filename, header)) return false;
  uint64_t num_pos = header.num_pos, block_size = header.block_size;

  struct stat st;
  if (stat(compressed_filename.c_str(), &st) != 0) return false;
  uint64_t filesize = st.st_size;

  uint64_t n_blocks = ceil((double) num_pos / block_size);
  bool large_blocks = block_size > uint64_t(UINT16_MAX);
  uint64_t block_size_bytes = large_blocks ? sizeof(uint32_t) : sizeof(uint16_t);
  uint64_t expected_size = header.header_size + n_blocks * block_size_bytes;
  if (filesize < expected_size) return false;

  FILE* f = fopen(compressed_filename.c_str(), "rb");
  if (f == NULL) return false;
  fseek(f, header.header_size, SEEK_SET);
  std::vector<uint8_t> block_sizes(n_blocks * block_size_bytes);
  size_t n = fread(block_sizes.data(), block_size_bytes, n_blocks, f);
  fclose(f);
  if (n != n_blocks) return false;

  for (uint64_t i = 0; i < n_blocks; i++) {
    expected_size += large_blocks ? ((uint32_t*) block_sizes.data())[i] : ((uint16_t*) block_sizes.data())[i];
  }
  return filesize == expected_size;
}

void recompress_TB(std::string source_filename, std::string target_filename, int nthreads, int compression_level, uint64_t block_size, bool verbose) {
  uint64_t old_checksum, num_pos, old_block_size;
  if (!read_compressed_header(source_filename, old_checksum, num_pos, old_block_size) || !is_complete_compressed_file(source_filename)) {
    std::cerr << "Invalid compressed file " << source_filename << std::endl;
    exit(1);
  }

  int16_t* TB = (int16_t*) malloc(num_pos * sizeof(int16_t));
  if (TB == NULL) {
    std::cerr << "Could not allocate " << num_pos * sizeof(int16_t) << " bytes." << std::endl;
    exit(1);
  }
  {
    // unmap source before writing, source may be equal to target
    CompressedTB ctb = CompressedTB(0, source_filename);
    ctb.decompress_to_array(nthreads, TB); // exits if the checksum of the source does not match
  }
  // the target is always written in the current version, whose checksum may differ from the source's
  uint64_t checksum = compute_checksum(TB, num_pos, nthreads);
  // writes to target_filename.tmp first and then moves it, so an interruption does not leave a partial file
  block_compress_TB(TB, num_pos, nthreads, compression_level, block_size, target_filename, true, verbose);
  free(TB);

  uint64_t new_checksum, new_num_pos, new_block_size;
  if (!read_compressed_header(target_filename, new_checksum, new_num_pos, new_block_size) || !is_complete_compressed_file(target_filename) ||
      new_checksum != checksum || new_num_pos != num_pos || new_block_size != block_size) {
    std::cerr << "Recompressed file " << target_filename << " is invalid." << std::endl;
    exit(1);
  }
}
