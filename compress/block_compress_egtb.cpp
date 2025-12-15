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

// #define LIBDEFLATE
// #define ZSTD
// #define LZ4

#ifdef LIBDEFLATE
  #include "libdeflate.h"
#endif
#ifdef ZSTD
  #include <zstd.h>
#endif
#ifdef LZ4
  #include <lz4hc.h>
#endif

uint64_t compute_checksum(int16_t* TB, uint64_t num_pos, int nthreads) {
  uint64_t s = 0;
    #pragma omp parallel for num_threads(nthreads) schedule(static) reduction(^:s)
    for (uint64_t ix = 0; ix < num_pos; ix++) {
      s ^= (ix << 10) | ((uint64_t) abs(TB[ix]));
    }
    return s;
}

uint64_t compress_egtb(std::string filename, int nthreads, int compression_level, uint64_t block_size, bool write, bool verbose) {

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


  if (verbose) std::cout << "Read " << num_pos << " entries (" << (double) num_pos*2 / (1024*1024*1024) << " GiB) from " << filename << std::endl;

  uint64_t checksum = compute_checksum(TB, num_pos, nthreads);

  uint64_t n_blocks = ceil((double) num_pos / block_size);
  uint64_t* block_sizes = (uint64_t*) malloc(n_blocks * sizeof(uint64_t));  

  bool large_blocks = block_size > uint64_t(UINT16_MAX); 
  uint64_t final_size = 0;

  #pragma omp parallel num_threads(nthreads)
  {
    // thread-private allocations
    std::vector<uint8_t> compressed(block_size*2);

    #ifdef LIBDEFLATE
      libdeflate_compressor* compressor = libdeflate_alloc_compressor(compression_level);
    #endif


    
    #pragma omp for schedule(static) reduction(+:final_size)
    for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
      uint64_t size = std::min(block_size, num_pos - start_ix) * sizeof(int16_t);
      uint64_t block_ix = start_ix / block_size;


      #ifdef LIBDEFLATE
        uint64_t compressed_size = libdeflate_deflate_compress(compressor, (uint8_t*) &TB[start_ix], size, compressed.data(), compressed.size());
      #endif
      #ifdef ZSTD
        uint64_t compressed_size = ZSTD_compress(compressed.data(), compressed.size(),  (uint8_t*) &TB[start_ix], size, compression_level);
        assert (!ZSTD_isError(compressed_size));
      #endif
      #ifdef LZ4
        uint64_t compressed_size = LZ4_compress_HC((char*) &TB[start_ix], (char*) compressed.data(), size, compressed.size(), compression_level);
      #endif
      

      if (compressed_size == 0) {
        std::cerr << "Compression failed\n";
        exit(1);
      }
      final_size += compressed_size;

      block_sizes[block_ix] = compressed_size;
      std::memcpy(&TB[start_ix], compressed.data(), compressed_size);
    }

    #ifdef LIBDEFLATE
      libdeflate_free_compressor(compressor);
    #endif
  }

  final_size += large_blocks ? sizeof(uint32_t) * n_blocks : sizeof(uint16_t) * n_blocks;
  final_size += sizeof(uint64_t); // checksum
  final_size += sizeof(uint64_t); // num_pos
  final_size += sizeof(uint64_t); // block_size

  if (verbose) std::cout << "final size: " << final_size << " " << (double) final_size / file_size << " (large_blocks = " << large_blocks << ")" << std::endl;


  if (write) {
    std::string compressed_filename = filename + ".bz";
    f = fopen(compressed_filename.c_str(), "wb");
    if (f == NULL) {
      std::cerr << "Error opening file: " << filename << std::endl;
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
  free(TB);
  return final_size;
}

struct CompressedEGTB {
  std::string filename;
  std::string egtb_id;
  uint64_t filesize;
  uint64_t checksum;
  uint64_t num_pos;
  uint64_t block_size;
  uint64_t n_blocks;
  uint64_t* block_sizes;
  uint64_t* block_offsets;
  uint8_t* map_ptr;
  uint8_t* compressed_blocks;
  int16_t* uncompressed_buf;
  #ifdef LIBDEFLATE
    libdeflate_decompressor* decompressor;
  #endif
  #ifdef ZSTD
    ZSTD_DCtx* decompressor;
  #endif

  CompressedEGTB(std::string filename) {
    this->filename = filename;

    size_t slash = filename.find_last_of('/');
    size_t dot = filename.find_last_of('.');
    this->egtb_id = filename.substr(slash + 1, dot - slash - 1);


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

    int headersize = 3*sizeof(uint64_t);
    this->checksum = ((uint64_t*) this->map_ptr)[0];
    this->num_pos = ((uint64_t*) this->map_ptr)[1];
    this->block_size = ((uint64_t*) this->map_ptr)[2];
    this->n_blocks = ceil((double) num_pos / block_size);
    
    bool large_blocks = block_size > uint64_t(UINT16_MAX);
    this->block_sizes = (uint64_t*) malloc(n_blocks * sizeof(uint64_t));
    if (large_blocks) {
      uint32_t* block_sizes_ptr = (uint32_t*) &this->map_ptr[headersize];
      for (uint64_t i = 0; i < n_blocks; i++) this->block_sizes[i] = block_sizes_ptr[i];
      this->compressed_blocks = &this->map_ptr[headersize + this->n_blocks*sizeof(uint32_t)];
    } else {
      uint16_t* block_sizes_ptr = (uint16_t*) &this->map_ptr[headersize];
      for (uint64_t i = 0; i < n_blocks; i++) this->block_sizes[i] = block_sizes_ptr[i];
      this->compressed_blocks = &this->map_ptr[headersize + this->n_blocks*sizeof(uint16_t)];
    }
    

    this->block_offsets = (uint64_t*) malloc(this->n_blocks * sizeof(uint64_t));
    this->uncompressed_buf = (int16_t*) malloc(block_size * sizeof(int16_t));

    uint64_t offset = 0;
    for (uint64_t block_ix = 0; block_ix < this->n_blocks; block_ix++) {
      this->block_offsets[block_ix] = offset;
      offset += this->block_sizes[block_ix];
    }

    #ifdef LIBDEFLATE
      this->decompressor = libdeflate_alloc_decompressor();
    #endif
    #ifdef ZSTD
      this->decompressor = ZSTD_createDCtx();
    #endif

    // std::cout << "CompressedEGTB" << this->egtb_id << ": filesize=" << this->filesize << ", num_pos=" << this->num_pos << ", block_size=" << this->block_size << ", n_blocks=" << this->n_blocks << std::endl;
  }

  ~CompressedEGTB() {
    // std::cout << "~CompressedEGTB" << this->egtb_id << std::endl;
    int unmap = munmap(this->map_ptr, this->filesize);
    if (unmap == -1) {
        perror("Error munmapping the file");
        exit(EXIT_FAILURE);
    }
    free(this->block_sizes);
    free(this->block_offsets);
    free(this->uncompressed_buf);

    #ifdef LIBDEFLATE
      libdeflate_free_decompressor(this->decompressor);
    #endif
    #ifdef ZSTD
      ZSTD_freeDCtx(this->decompressor);
    #endif
  }

  int16_t get(uint64_t ix) {
    uint64_t block_ix = ix / block_size;
    uint64_t ix_in_block = ix % block_size;
    uint64_t offset = block_offsets[block_ix];
    uint64_t size = std::min(block_size, num_pos - block_ix*block_size) * sizeof(int16_t);

    #ifdef LIBDEFLATE
      libdeflate_result res = libdeflate_deflate_decompress(decompressor,
        &compressed_blocks[offset], block_sizes[block_ix],
        uncompressed_buf, size, NULL);
      assert (res == 0);
    #endif
    #ifdef ZSTD
      size_t res = ZSTD_decompressDCtx(decompressor,
        uncompressed_buf, size,
        &compressed_blocks[offset], block_sizes[block_ix]);
      assert (!ZSTD_isError(res));
    #endif
    #ifdef LZ4
    int res = LZ4_decompress_safe(
      (char*) &compressed_blocks[offset], (char*) uncompressed_buf,
      block_sizes[block_ix], size);
    assert (res > 0);
    #endif
    return uncompressed_buf[ix_in_block];
  }

  int16_t* decompress_into_array(int nthreads) {
    int16_t* TB = (int16_t*) malloc(num_pos * sizeof(int16_t));

    #pragma omp parallel num_threads(nthreads)
    {
      #ifdef LIBDEFLATE
        libdeflate_decompressor* local_decompressor = libdeflate_alloc_decompressor();
      #endif

      #pragma omp for schedule(static)
      for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
        uint64_t block_ix = start_ix / block_size;
        uint64_t offset = block_offsets[block_ix];
        uint64_t size = std::min(block_size, num_pos - start_ix) * sizeof(int16_t);
        #ifdef LIBDEFLATE
          libdeflate_result res = libdeflate_deflate_decompress(local_decompressor,
            &compressed_blocks[offset], block_sizes[block_ix],
            &TB[start_ix], size, NULL);
          assert (res == 0);
        #endif
        #ifdef ZSTD
          size_t res = ZSTD_decompress(
            &TB[start_ix], size,
            &compressed_blocks[offset], block_sizes[block_ix]);
          assert (!ZSTD_isError(res));
        #endif
        #ifdef LZ4
        int res = LZ4_decompress_safe(
          (char*) &compressed_blocks[offset], (char*) &TB[start_ix],
          block_sizes[block_ix], size);
        assert (res > 0);
        #endif
      }

      #ifdef LIBDEFLATE
        libdeflate_free_decompressor(local_decompressor);
      #endif
    }

    assert (compute_checksum(TB, num_pos, nthreads) == checksum);

    return TB;
  }
};

int main_test(int argc, char *argv[]) {
  assert(argc == 4);
  uint64_t n_threads = atoi(argv[1]);
  uint64_t compression_level = atoi(argv[2]);
  uint64_t block_size = atoi(argv[3]); // [4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152]

  #ifdef LIBDEFLATE
  std::cout << "LIBDEFLATE" << std::endl;
  #endif
  #ifdef ZSTD
  std::cout << "ZSTD" << std::endl;
  #endif
  #ifdef LZ4
  std::cout << "LZ4" << std::endl;
  #endif
  std::cout << "n_threads: " << n_threads << ", compression_level: " << compression_level << ", block_size: " << block_size << std::endl;
  bool verbose = true;
  bool write = true;
  bool test = true;

  uint64_t count = 0;
  uint64_t total_size = 0;
  uint64_t total_compressed_size = 0;
  uint64_t total_zipped_size = 0;
  std::string path = "egtbs/5men/";
  std::vector<std::string> files;
  TimePoint t0 = now();
  for (const auto & entry : fs::recursive_directory_iterator(path)) {
    if (entry.path().extension() != ".egtb") continue;
    count++;
    files.push_back(entry.path().u8string());
    uint64_t size = entry.file_size();
    total_size += size;
    uint64_t zipped_size = fs::file_size(fs::path(entry.path().u8string() + ".zip"));
    total_zipped_size += zipped_size;
    uint64_t compressed_size = compress_egtb(entry.path().u8string(), n_threads, compression_level, block_size, write, verbose);
    total_compressed_size += compressed_size;
    if (verbose) {
      std::cout << entry.path() << " size: " << size;
      std::cout << " zipped size: " << zipped_size << " (" << (double) zipped_size / size << ")";
      std::cout << " compressed size: " << compressed_size << " (" << (double) compressed_size / size << ")";
      std::cout << (compressed_size <= zipped_size ? " OK" : " :(") << std::endl;
      std::cout << std::endl;
    }
  }
  std::cout << "Compressed " << count << " files" << std::endl;
  std::cout << "TOTAL" << " size: " << total_size;
  std::cout << " zipped size: " << total_zipped_size << " (" << (double) total_zipped_size / total_size << ")";
  std::cout << " compressed size: " << total_compressed_size << " (" << (double) total_compressed_size / total_size << ")" << std::endl;

  TimePoint t1 = now();
  std::cout << "Finished in " << (double) (t1 - t0) / 1000 << " s" << std::endl;

  if (test) {
    CompressedEGTB** cegtbs = (CompressedEGTB**) malloc(count * sizeof(CompressedEGTB*));
    for (uint64_t i = 0; i < count; i++) {
      cegtbs[i] = new CompressedEGTB(files[i] + ".bz");
      int16_t* decompressed_TB = cegtbs[i]->decompress_into_array(n_threads);
      free(decompressed_TB);
    }
    
    int n = 100000;
    t0 = now();
    for (int i = 0; i < n; i++) {
      int fix = rand() % count;
      CompressedEGTB* cegtb = cegtbs[fix];
      uint64_t ix = rand() % cegtb->num_pos;
      cegtb->get(ix);
    }
    t1 = now();
    std::cout << "Mean access time " << (double) (t1 - t0) /n << "ms vs filesize " << (double) total_compressed_size / (1024*1024*1024) <<  "GiB" << std::endl;
    
    for (uint64_t i = 0; i < count; i++) {
      free(cegtbs[i]);
    }
    free(cegtbs);
  }

  return 0;
}

void run_cmd(std::string cmd) {
  std::cout << "CMD " << cmd << std::endl;
  system(cmd.c_str());
}

int main_compress(int argc, char *argv[]) {
  assert(argc == 4);
  uint64_t n_threads = atoi(argv[1]);
  uint64_t compression_level = atoi(argv[2]);
  uint64_t block_size = atoi(argv[3]);

  #ifdef LIBDEFLATE
  std::cout << "LIBDEFLATE" << std::endl;
  #endif
  #ifdef ZSTD
  std::cout << "ZSTD" << std::endl;
  #endif
  #ifdef LZ4
  std::cout << "LZ4" << std::endl;
  #endif
  std::cout << "n_threads: " << n_threads << ", compression_level: " << compression_level << ", block_size: " << block_size << std::endl;
  bool verbose = true;
  bool write = true;

  uint64_t count = 0;
  uint64_t total_size = 0;
  uint64_t total_compressed_size = 0;
  uint64_t total_zipped_size = 0;
  std::string path = "egtbs/";
  for (const auto & entry : fs::recursive_directory_iterator(path)) {
    if (entry.path().extension() != ".zip") continue;
    TimePoint t0 = now();
    count++;
    std::string zipped_filename = entry.path().u8string();
    std::string filename = zipped_filename.substr(0, zipped_filename.size() - 4);
    std::cout << count << ": " << zipped_filename << " " << filename << std::endl;

    run_cmd("unzip -n " + zipped_filename);
    run_cmd("md5sum " + filename);

    uint64_t zipped_size = entry.file_size();
    total_zipped_size += zipped_size;
    uint64_t size = fs::file_size(fs::path(filename));
    total_size += size;
    uint64_t compressed_size = compress_egtb(filename, n_threads, compression_level, block_size, write, verbose);
    total_compressed_size += compressed_size;

    run_cmd("rm " + zipped_filename);
    run_cmd("rm " + filename);

    TimePoint t1 = now();
    if (verbose) {
      std::cout << filename << " size: " << size;
      std::cout << " zipped size: " << zipped_size << " (" << (double) zipped_size / size << ")";
      std::cout << " compressed size: " << compressed_size << " (" << (double) compressed_size / size << ")";
      std::cout << (compressed_size <= zipped_size ? " OK" : " :(") << std::endl;
      std::cout << "Finished in " << (double) (t1 - t0) / 1000 << " s" << std::endl;
      std::cout << std::endl;
    }

  }
  std::cout << "Compressed " << count << " files" << std::endl;
  std::cout << "TOTAL" << " size: " << total_size;
  std::cout << " zipped size: " << total_zipped_size << " (" << (double) total_zipped_size / total_size << ")";
  std::cout << " compressed size: " << total_compressed_size << " (" << (double) total_compressed_size / total_size << ")" << std::endl;

  return 0;
}

int main(int argc, char *argv[]) {
  // main_test(argc, argv);
  main_compress(argc, argv); // ./compress/compress_zstd.out 32 19 1048576 |& tee -a log_compress.txt
}