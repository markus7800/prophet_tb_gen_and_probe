#include <omp.h>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <cstdint>
#include <cstring>
#include <math.h>
#include <unistd.h> // close
#include <fcntl.h> // open
#include <sys/mman.h> // mmap
#include <sys/stat.h> // file size
#include "../misc.h"
namespace fs = std::filesystem;

#include "libdeflate.h"
#include <zstd.h>


uint64_t compress_egtb(std::string filename, int nthreads, int compression_level, uint64_t block_size, bool write) {

  // load file
  FILE* f = fopen(filename.c_str(), "rb");
  if (f == NULL) {
    std::cerr << "Error opening file: " << filename << std::endl;
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  uint64_t file_size = ftell(f);
  rewind(f);
  
  uint64_t num_pos = file_size / sizeof(int16_t);
  int16_t *TB = (int16_t*) malloc(num_pos * sizeof(int16_t));
  
  fread(TB, sizeof(int16_t), num_pos, f);
  fclose(f);


  std::cout << "Read " << num_pos << " entries (" << (double) num_pos*2 / (1024*1024*1024) << " GiB) from " << filename << std::endl;

  uint64_t n_blocks = ceil((double) num_pos / block_size);
  uint16_t* block_sizes = (uint16_t*) malloc(n_blocks * sizeof(uint16_t));  

  bool large_blocks = false; 
  uint64_t final_size = 0;

  #pragma omp parallel num_threads(nthreads)
  {
    // thread-private allocations
    // libdeflate_compressor* compressor = libdeflate_alloc_compressor(compression_level);
    std::vector<uint8_t> compressed(block_size*2);

    
    #pragma omp for schedule(static) reduction(+:final_size) reduction(||: large_blocks)
    for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
      uint64_t size = std::min(block_size, num_pos - start_ix) * sizeof(int16_t);
      uint64_t block_ix = start_ix / block_size;

      // uint64_t compressed_size = libdeflate_deflate_compress(compressor, (uint8_t*) &TB[start_ix], size, compressed.data(), compressed.size());
      uint64_t compressed_size = ZSTD_compress(compressed.data(), compressed.size(),  (uint8_t*) &TB[start_ix], size, compression_level);

      if (compressed_size == 0) {
        std::cerr << "Compression failed\n";
        exit(1);
      }
      large_blocks = large_blocks || (compressed_size > UINT16_MAX);
      final_size += compressed_size;

      assert (compressed_size <= UINT16_MAX);
      assert (compressed_size <= size);
      block_sizes[block_ix] = compressed_size;
      std::memcpy(&TB[start_ix], compressed.data(), compressed_size);
    }

    // libdeflate_free_compressor(compressor);
  }

  final_size += large_blocks ? sizeof(uint32_t) * n_blocks : sizeof(uint16_t) * n_blocks;
  final_size += sizeof(uint64_t); // num_pos
  final_size += sizeof(uint64_t); // block_size

  std::cout << "final size: " << final_size << " " << (double) final_size / file_size << " (large_blocks = " << large_blocks << ")" << std::endl;


  if (write) {
    std::string compressed_filename = filename + ".bz";
    f = fopen(compressed_filename.c_str(), "wb");
    if (f == NULL) {
      std::cerr << "Error opening file: " << filename << std::endl;
      exit(1);
    }
    fwrite(&num_pos, sizeof(uint64_t), 1, f);
    fwrite(&block_size, sizeof(uint64_t), 1, f);
    fwrite(block_sizes, sizeof(uint16_t), n_blocks, f);

    for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
      uint64_t block_ix = start_ix / block_size;
      fwrite(&TB[start_ix], sizeof(uint8_t), block_sizes[block_ix], f);
      assert(block_ix * block_size == start_ix);
    }
    std::cout << "Wrote to " << compressed_filename << std::endl;
    fclose(f);
  }

  free(TB);
  return final_size;
}

struct CompressedEGTB {
  std::string filename;
  uint64_t filesize;
  uint64_t num_pos;
  uint64_t block_size;
  uint64_t n_blocks;
  uint16_t* block_sizes;
  uint64_t* block_offsets;
  uint8_t* map_ptr;
  uint8_t* compressed_blocks;
  int16_t* uncompressed_buf;
  libdeflate_decompressor* decompressor;

  CompressedEGTB(std::string filename) {
    this->filename = filename;

    struct stat st;
    stat(filename.c_str(), &st);
    this->filesize = st.st_size;
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1) {
        printf("Could not open file %s.\n", filename.c_str());
        exit(EXIT_FAILURE);
    }
    this->map_ptr = (uint8_t*) mmap(0, this->filesize, PROT_READ, MAP_SHARED, fd, 0);
    if (this->map_ptr == MAP_FAILED) {
        close(fd);
        perror("Error mmapping the file");
        exit(EXIT_FAILURE);
    }
    close(fd);

    this->num_pos = ((uint64_t*) this->map_ptr)[0];
    this->block_size = ((uint64_t*) this->map_ptr)[1];
    this->n_blocks = ceil((double) num_pos / block_size);
    this->block_sizes = (uint16_t*) &this->map_ptr[2*sizeof(uint64_t)];
    this->compressed_blocks = &this->map_ptr[2*sizeof(uint64_t) + this->n_blocks*sizeof(uint16_t)];
    this->block_offsets = (uint64_t*) malloc(this->n_blocks * sizeof(uint64_t));
    this->uncompressed_buf = (int16_t*) malloc(block_size * sizeof(int16_t));

    uint64_t offset = 0;
    for (uint64_t block_ix = 0; block_ix < this->n_blocks; block_ix++) {
      this->block_offsets[block_ix] = offset;
      offset += this->block_sizes[block_ix];
    }

    this->decompressor = libdeflate_alloc_decompressor();

    std::cout << "CompressedEGTB: filesize=" << this->filesize << ", num_pos=" << this->num_pos << ", block_size=" << this->block_size << ", n_blocks=" << this->n_blocks << std::endl;
  }

  ~CompressedEGTB() {
    int unmap = munmap(this->map_ptr, this->filesize);
    if (unmap == -1) {
        perror("Error munmapping the file");
        exit(EXIT_FAILURE);
    }
    free(this->block_offsets);
    free(this->uncompressed_buf);
    libdeflate_free_decompressor(this->decompressor);
  }

  int16_t get(uint64_t ix) {
    uint64_t block_ix = ix / block_size;
    uint64_t ix_in_block = ix % block_size;
    uint64_t offset = block_offsets[block_ix];
    // uint64_t size = std::min(block_size, num_pos - block_ix*block_size) * sizeof(int16_t);
    // libdeflate_result res = libdeflate_deflate_decompress(decompressor,
    //   &compressed_blocks[offset], block_sizes[block_ix],
    //   uncompressed_buf, size, NULL);
    // if (res != 0) {
    //   std::cout << "libdeflate_result: " << res << std::endl;
    //   exit(1);
    // }
    size_t res = ZSTD_decompress(uncompressed_buf, block_size * sizeof(int16_t), &compressed_blocks[offset], block_sizes[block_ix]);
    assert (!ZSTD_isError(res));
    return uncompressed_buf[ix_in_block];
  }
};

int main(int argc, char *argv[]) {
  assert(argc > 3);
  uint64_t n_threads = atoi(argv[1]);
  uint64_t compression_level = atoi(argv[2]);
  uint64_t block_size = atoi(argv[3]); // [4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152]
  if (argc == 4) {
    std::cout << "n_threads: " << n_threads << ", compression_level: " << compression_level << ", block_size: " << block_size << std::endl;
    
    uint64_t count = 0;
    uint64_t total_size = 0;
    uint64_t total_compressed_size = 0;
    uint64_t total_zipped_size = 0;
    std::string path = "../egtbs/5men/";
    for (const auto & entry : fs::recursive_directory_iterator(path)) {
      if (entry.path().extension() != ".egtb") continue;
      count++;
      uint64_t size = entry.file_size();
      total_size += size;
      uint64_t zipped_size = fs::file_size(fs::path(entry.path().u8string() + ".zip"));
      total_zipped_size += zipped_size;
      uint64_t compressed_size = compress_egtb(entry.path().u8string(), n_threads, compression_level, block_size, false);
      total_compressed_size += compressed_size;
      std::cout << entry.path() << " size: " << size;
      std::cout << " zipped size: " << zipped_size << " (" << (double) zipped_size / size << ")";
      std::cout << " compressed size: " << compressed_size << " (" << (double) compressed_size / size << ")";
      std::cout << (compressed_size <= zipped_size ? " OK" : " :(") << std::endl;
      std::cout << std::endl;
    }
    std::cout << "Compressed " << count << " files" << std::endl;
    std::cout << "TOTAL" << " size: " << total_size;
    std::cout << " zipped size: " << total_zipped_size << " (" << (double) total_zipped_size / total_size << ")";
    std::cout << " compressed size: " << total_compressed_size << " (" << (double) total_compressed_size / total_size << ")" << std::endl;

  // zip size: 8709004054 (0.144411)

  // libdeflate levels = 1 ... 12

  // compression_level = 9 block_size = 32768 -> 8563460076 (0.141998) 3m20
  // compression_level = 10 block_size = 32768 -> 7936397012 (0.1316) 7m BEST
  // compression_level = 12 block_size = 32768 -> 7785773584 (0.129102) 19m

  // compression_level = 9 block_size = 8192 -> 9137047977 (0.151509) 3m
  // compression_level = 10 block_size = 8192 -> 8581590257 (0.142298) 6m20
  // compression_level = 12 block_size = 8192 -> 8416370763 (0.139559) 17m

  // compression_level = 10 block_size = 1048576 ->  7627181263 (0.126473) 8m

  // zstd levels = 1 ... 22

  // compression_level = 10 block_size = 32768 -> 8401179133 (0.139307) 2m
  // compression_level = 16 block_size = 32768 -> 7264373856 (0.120457) 12m

  } else {
  
    assert(argc == 5);
    std::string filename = argv[4];
    // ../egtbs/5men/1pawns/KRKRP.egtb

    compress_egtb(filename, n_threads, compression_level, block_size, true);

    CompressedEGTB cegtb = CompressedEGTB(filename + ".bz");

    // load file
    FILE* f = fopen(filename.c_str(), "rb");
    if (f == NULL) {
      std::cerr << "Error opening file: " << filename << std::endl;
      exit(1);
    }
    fseek(f, 0, SEEK_END);
    uint64_t file_size = ftell(f);
    rewind(f);
    
    uint64_t num_pos = file_size / sizeof(int16_t);
    int16_t *TB = (int16_t*) malloc(num_pos * sizeof(int16_t));
    
    fread(TB, sizeof(int16_t), num_pos, f);
    fclose(f);

    int n = 100000;
    TimePoint t0 = now();
    for (int i = 0; i < n; i++) {
      uint64_t ix = rand() % cegtb.num_pos;
      assert (cegtb.get(ix) == TB[ix]);
    }
    TimePoint t1 = now();
    std::cout << "Mean access time " << (double) (t1 - t0) /n << " vs filesize " << (double) cegtb.filesize / (1024*1024*1024) <<  "GiB" << std::endl;
    free(TB);
  }

  return 0;
}