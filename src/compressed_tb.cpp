
#include "compressed_tb.h"
#include <vector>
#include <cstring>
#include <algorithm>

uint64_t compute_checksum(int16_t* TB, uint64_t num_pos, [[maybe_unused]] int nthreads) {
  uint64_t s = 0;
  #pragma omp parallel for num_threads(nthreads) schedule(static) reduction(^:s)
  for (uint64_t ix = 0; ix < num_pos; ix++) {
    s ^= (ix << 10) | ((uint64_t) abs(TB[ix]));
  }
  return s;
}

uint64_t compute_checksum_WDL(uint8_t* WDL, uint64_t num_bytes, [[maybe_unused]] int nthreads) {
  uint64_t s = 0;
  #pragma omp parallel for num_threads(nthreads) schedule(static) reduction(^:s)
  for (uint64_t ix = 0; ix < num_bytes; ix++) {
    s ^= (ix << 10) | ((uint64_t) WDL[ix]);
  }
  return s;
}

bool is_wdl_filename(std::string compressed_filename) {
  std::string ext = std::string(WDL_EXT) + COMP_EXT;
  return compressed_filename.size() >= ext.size() && compressed_filename.compare(compressed_filename.size() - ext.size(), ext.size(), ext) == 0;
}

bool CompressedTB::check_integrity([[maybe_unused]] int nthreads) {
  uint64_t s = 0;

  #pragma omp parallel num_threads(nthreads)
  {
    DecompressCtx* dctx = new DecompressCtx(get_max_uncompressed_block_size());
    uint64_t local_s = 0;

    #pragma omp for schedule(static)
    for (uint64_t block_ix = 0; block_ix < n_blocks; block_ix++) {
      decompress_block(block_ix, dctx->uncompressed_buf, dctx->decompressor);

      if (wdl) {
        uint64_t start_ix = block_ix * (block_size / 8);
        uint64_t block_nbytes = get_uncompressed_block_size(block_ix);
        for (uint64_t ix = start_ix; ix < start_ix + block_nbytes; ix++) {
          local_s ^= (ix << 10) | ((uint64_t) dctx->uncompressed_buf[ix - start_ix]);
        }
      } else {
        int16_t* buf = (int16_t*) dctx->uncompressed_buf;
        uint64_t start_ix = block_ix * block_size;
        uint64_t block_npos = std::min(block_size, num_pos - start_ix);
        for (uint64_t ix = start_ix; ix < start_ix + block_npos; ix++) {
          local_s ^= (ix << 10) | ((uint64_t) abs(buf[ix - start_ix]));
        }
      }
    }

    #pragma omp critical
    {
      s ^= local_s;
      delete dctx;
    }
  }

  return (s == checksum);
}

// compresses data of total_bytes bytes in blocks of block_bytes bytes, where each block corresponds to block_size positions
// compressed blocks are stored in place in data
uint64_t block_compress_bytes(uint8_t* data, uint64_t total_bytes, uint64_t block_bytes, uint64_t checksum, uint64_t num_pos, [[maybe_unused]] int nthreads, int compression_level, uint64_t block_size, std::string compressed_filename, bool write, bool verbose) {
  uint64_t n_blocks = ceil((double) num_pos / block_size);
  assert (n_blocks == total_bytes / block_bytes + (total_bytes % block_bytes != 0));
  uint64_t* block_sizes = (uint64_t*) malloc(n_blocks * sizeof(uint64_t));  

  bool large_blocks = block_size > uint64_t(UINT16_MAX);
  uint64_t max_compressed_block_size = large_blocks ? UINT32_MAX : UINT16_MAX;
  uint64_t final_size = 0;

  // compressed blocks are stored in place in data, blocks that do not fit (e.g. a tiny last block) are stored here
  // kept for safety but very unlikely
  std::vector<std::pair<uint64_t, std::vector<uint8_t>>> overflow_blocks;

  #pragma omp parallel num_threads(nthreads)
  {
    // thread-private allocations
    std::vector<uint8_t> compressed(ZSTD_compressBound(block_bytes));


    #pragma omp for schedule(static) reduction(+:final_size)
    for (uint64_t block_ix = 0; block_ix < n_blocks; block_ix++) {
      uint64_t start = block_ix * block_bytes;
      uint64_t size = std::min(block_bytes, total_bytes - start);

      uint64_t compressed_size = ZSTD_compress(compressed.data(), compressed.size(), &data[start], size, compression_level);

      if (ZSTD_isError(compressed_size) || compressed_size == 0) {
        std::cerr << "Compression of block " << block_ix << " failed: " << ZSTD_getErrorName(compressed_size) << std::endl;
        exit(1);
      }
      if (compressed_size > max_compressed_block_size) {
        std::cerr << "Compressed size " << compressed_size << " of block " << block_ix << " does not fit into the block size table (max " << max_compressed_block_size << "). ";
        std::cerr << "Use a smaller block size or one larger than " << UINT16_MAX << "." << std::endl;
        exit(1);
      }
      final_size += compressed_size;

      block_sizes[block_ix] = compressed_size;
      if (compressed_size <= size) {
        std::memcpy(&data[start], compressed.data(), compressed_size);
      } else {
        #pragma omp critical
        overflow_blocks.emplace_back(block_ix, std::vector<uint8_t>(compressed.begin(), compressed.begin() + compressed_size));
      }
    }

  }
  std::sort(overflow_blocks.begin(), overflow_blocks.end());

  final_size += large_blocks ? sizeof(uint32_t) * n_blocks : sizeof(uint16_t) * n_blocks;
  final_size += sizeof(uint64_t); // checksum
  final_size += sizeof(uint64_t); // num_pos
  final_size += sizeof(uint64_t); // block_size

  if (write) {
    FILE* f = fopen((compressed_filename + ".tmp").c_str(), "wb");
    if (f == NULL) {
      std::cerr << "Error opening file: " << compressed_filename << std::endl;
      exit(1);
    }
    fwrite(&checksum, sizeof(uint64_t), 1, f);
    fwrite(&num_pos, sizeof(uint64_t), 1, f);
    fwrite(&block_size, sizeof(uint64_t), 1, f);
    if (large_blocks) {
      std::vector<uint32_t> write_block_sizes(block_sizes, block_sizes + n_blocks);
      fwrite(write_block_sizes.data(), sizeof(uint32_t), n_blocks, f);
    } else {
      std::vector<uint16_t> write_block_sizes(block_sizes, block_sizes + n_blocks);
      fwrite(write_block_sizes.data(), sizeof(uint16_t), n_blocks, f);
    }

    auto overflow_block = overflow_blocks.begin();
    for (uint64_t block_ix = 0; block_ix < n_blocks; block_ix++) {
      if (overflow_block != overflow_blocks.end() && overflow_block->first == block_ix) {
        fwrite(overflow_block->second.data(), sizeof(uint8_t), block_sizes[block_ix], f);
        overflow_block++;
      } else {
        fwrite(&data[block_ix * block_bytes], sizeof(uint8_t), block_sizes[block_ix], f);
      }
    }
    fclose(f);
    system(("mv " + compressed_filename + ".tmp " + compressed_filename).c_str());
    if (verbose) std::cout << "Wrote to " << compressed_filename << std::endl;
  }
  
  if (verbose) std::cout << "Compressed final size: " << final_size << " ratio: " << (double) final_size / total_bytes << std::endl;


  free(block_sizes);
  return final_size;
}

uint64_t block_compress_TB(int16_t* TB, uint64_t num_pos, int nthreads, int compression_level, uint64_t block_size, std::string compressed_filename, bool write, bool verbose) {
  uint64_t checksum = compute_checksum(TB, num_pos, nthreads);
  return block_compress_bytes((uint8_t*) TB, num_pos * sizeof(int16_t), block_size * sizeof(int16_t), checksum,
    num_pos, nthreads, compression_level, block_size, compressed_filename, write, verbose);
}

uint64_t block_compress_WDL(uint8_t* WDL, uint64_t num_pos, int nthreads, int compression_level, uint64_t block_size, std::string compressed_filename, bool write, bool verbose) {
  assert (block_size % 8 == 0);
  uint64_t num_bytes = wdl_num_bytes(num_pos);
  uint64_t checksum = compute_checksum_WDL(WDL, num_bytes, nthreads);
  return block_compress_bytes(WDL, num_bytes, block_size / 8, checksum,
    num_pos, nthreads, compression_level, block_size, compressed_filename, write, verbose);
}


bool read_compressed_header(std::string compressed_filename, uint64_t& checksum, uint64_t& num_pos, uint64_t& block_size) {
  FILE* f = fopen(compressed_filename.c_str(), "rb");
  if (f == NULL) return false;
  uint64_t header[3];
  size_t n = fread(header, sizeof(uint64_t), 3, f);
  fclose(f);
  if (n != 3) return false;
  checksum = header[0];
  num_pos = header[1];
  block_size = header[2];
  return block_size > 0;
}

bool is_complete_compressed_file(std::string compressed_filename) {
  uint64_t checksum, num_pos, block_size;
  if (!read_compressed_header(compressed_filename, checksum, num_pos, block_size)) return false;

  struct stat st;
  if (stat(compressed_filename.c_str(), &st) != 0) return false;
  uint64_t filesize = st.st_size;

  uint64_t n_blocks = ceil((double) num_pos / block_size);
  bool large_blocks = block_size > uint64_t(UINT16_MAX);
  uint64_t block_size_bytes = large_blocks ? sizeof(uint32_t) : sizeof(uint16_t);
  uint64_t expected_size = 3*sizeof(uint64_t) + n_blocks * block_size_bytes;
  if (filesize < expected_size) return false;

  FILE* f = fopen(compressed_filename.c_str(), "rb");
  if (f == NULL) return false;
  fseek(f, 3*sizeof(uint64_t), SEEK_SET);
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
  uint64_t checksum, num_pos, old_block_size;
  if (!read_compressed_header(source_filename, checksum, num_pos, old_block_size) || !is_complete_compressed_file(source_filename)) {
    std::cerr << "Invalid compressed file " << source_filename << std::endl;
    exit(1);
  }
  bool wdl = is_wdl_filename(source_filename);
  if (wdl && block_size % 8 != 0) {
    std::cerr << "Block size of WDL file has to be divisible by 8." << std::endl;
    exit(1);
  }

  uint8_t* data;
  {
    // unmap source before writing, source may be equal to target
    CompressedTB ctb = CompressedTB(0, source_filename, wdl);
    data = (uint8_t*) malloc(ctb.get_uncompressed_size());
    if (data == NULL) {
      std::cerr << "Could not allocate " << ctb.get_uncompressed_size() << " bytes." << std::endl;
      exit(1);
    }
    if (wdl) ctb.decompress_to_wdl_array(nthreads, data);
    else ctb.decompress_to_array(nthreads, (int16_t*) data);
  }
  // writes to target_filename.tmp first and then moves it, so an interruption does not leave a partial file
  if (wdl) block_compress_WDL(data, num_pos, nthreads, compression_level, block_size, target_filename, true, verbose);
  else block_compress_TB((int16_t*) data, num_pos, nthreads, compression_level, block_size, target_filename, true, verbose);
  free(data);

  uint64_t new_checksum, new_num_pos, new_block_size;
  if (!read_compressed_header(target_filename, new_checksum, new_num_pos, new_block_size) || !is_complete_compressed_file(target_filename) ||
      new_checksum != checksum || new_num_pos != num_pos || new_block_size != block_size) {
    std::cerr << "Recompressed file " << target_filename << " is invalid." << std::endl;
    exit(1);
  }
}
