
#include "compressed_tb.h"
#include <vector>
#include <cstring>

uint64_t compute_checksum(int16_t* TB, uint64_t num_pos, int nthreads) {
  uint64_t s = 0;
    #pragma omp parallel for num_threads(nthreads) schedule(static) reduction(^:s)
    for (uint64_t ix = 0; ix < num_pos; ix++) {
      s ^= (ix << 10) | ((uint64_t) abs(TB[ix]));
    }
    return s;
}

uint64_t compress_egtb(int16_t* TB, uint64_t num_pos, int nthreads, int compression_level, uint64_t block_size, std::string compressed_filename, bool write, bool verbose) {
  uint64_t tb_size_bytes = num_pos * sizeof(int16_t);
  uint64_t checksum = compute_checksum(TB, num_pos, nthreads);

  uint64_t n_blocks = ceil((double) num_pos / block_size);
  uint64_t* block_sizes = (uint64_t*) malloc(n_blocks * sizeof(uint64_t));  

  bool large_blocks = block_size > uint64_t(UINT16_MAX); 
  uint64_t final_size = 0;

  #pragma omp parallel num_threads(nthreads)
  {
    // thread-private allocations
    std::vector<uint8_t> compressed(block_size * sizeof(int16_t));

    
    #pragma omp for schedule(static) reduction(+:final_size)
    for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
      uint64_t size = std::min(block_size, num_pos - start_ix) * sizeof(int16_t);
      uint64_t block_ix = start_ix / block_size;

      uint64_t compressed_size = ZSTD_compress(compressed.data(), compressed.size(),  (uint8_t*) &TB[start_ix], size, compression_level);
      assert (!ZSTD_isError(compressed_size));


      if (compressed_size == 0) {
        std::cerr << "Compression failed\n";
        exit(1);
      }
      final_size += compressed_size;

      block_sizes[block_ix] = compressed_size;
      std::memcpy(&TB[start_ix], compressed.data(), compressed_size);
    }

  }

  final_size += large_blocks ? sizeof(uint32_t) * n_blocks : sizeof(uint16_t) * n_blocks;
  final_size += sizeof(uint64_t); // checksum
  final_size += sizeof(uint64_t); // num_pos
  final_size += sizeof(uint64_t); // block_size

  if (verbose) std::cout << "final size: " << final_size << " " << (double) final_size / tb_size_bytes << " (large_blocks = " << large_blocks << ")" << std::endl;


  if (write) {
    FILE* f = fopen(compressed_filename.c_str(), "wb");
    if (f == NULL) {
      std::cerr << "Error opening file: " << compressed_filename << std::endl;
      exit(1);
    }
    fwrite(&checksum, sizeof(uint64_t), 1, f);
    fwrite(&num_pos, sizeof(uint64_t), 1, f);
    fwrite(&block_size, sizeof(uint64_t), 1, f);
    if (large_blocks) {
      uint32_t* write_block_sizes = (uint32_t*) malloc(n_blocks * sizeof(uint32_t));  
      for (uint64_t i = 0; i < n_blocks; i++) write_block_sizes[i] = block_sizes[i];
      fwrite(write_block_sizes, sizeof(uint32_t), n_blocks, f);
    } else {
      uint16_t* write_block_sizes = (uint16_t*) malloc(n_blocks * sizeof(uint16_t));  
      for (uint64_t i = 0; i < n_blocks; i++) write_block_sizes[i] = block_sizes[i];
      fwrite(write_block_sizes, sizeof(uint16_t), n_blocks, f);
    }

    for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
      uint64_t block_ix = start_ix / block_size;
      fwrite(&TB[start_ix], sizeof(uint8_t), block_sizes[block_ix], f);
      assert(block_ix * block_size == start_ix);
    }
    if (verbose) std::cout << "Wrote to " << compressed_filename << std::endl;
    fclose(f);
  }

  free(block_sizes);
  return final_size;
}

