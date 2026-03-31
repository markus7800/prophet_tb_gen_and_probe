
#include <iostream>
#include <cassert>
#include "egtb.h"
#include "compressed_tb.h"
#include "misc.h"
#include <filesystem>
namespace fs = std::filesystem;

using namespace Prophet;

void load_file(std::string filename, int16_t*& TB, u_int64_t& num_pos, bool verbose) {
  // load file
  FILE* f = fopen(filename.c_str(), "rb");
  if (f == NULL) {
    std::cerr << "Error opening file: " << filename << std::endl;
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  uint64_t file_size = ftell(f);
  rewind(f);
  
  num_pos = file_size / sizeof(int16_t);
  TB = (int16_t*) malloc(num_pos * sizeof(int16_t));
  
  fread(TB, sizeof(int16_t), num_pos, f);
  fclose(f);
  if (verbose) std::cout << "Read " << num_pos << " entries (" << (double) num_pos*2 / (1024*1024*1024) << " GiB) from " << filename << std::endl;
}

void run_cmd(std::string cmd) {
  std::cout << "CMD " << cmd << std::endl;
  system(cmd.c_str());
}


void recompress_files(std::string path, uint64_t n_threads, uint64_t compression_level, uint64_t block_size, bool verbose, bool write, bool bench) {
  std::cout << "n_threads: " << n_threads << ", compression_level: " << compression_level << ", block_size: " << block_size << std::endl;

  uint64_t count = 0;
  uint64_t total_size = 0;
  uint64_t total_compressed_size = 0;
  
  std::vector<std::string> files;
  
  TimePoint t0 = now();
  for (const auto & entry : fs::recursive_directory_iterator(path)) {
    if (entry.path().extension() != COMP_EXT) continue;
    std::string bz_filename = entry.path().u8string();
    files.push_back(bz_filename);
    count++;
    std::string filename = bz_filename.substr(0, bz_filename.size() - 3);
    CompressedTB ctb = CompressedTB(0, bz_filename);
    uint64_t size = ctb.num_pos * sizeof(int16_t);
    total_size += size;
    uint64_t compressed_size;
    TimePoint tt0 = now();
    if (ctb.block_size != block_size) {
      int16_t* TB = (int16_t*) malloc(size);
      ctb.decompress_to_array(n_threads, TB);
      compressed_size = block_compress_TB(TB, ctb.num_pos, n_threads, compression_level, block_size, filename + COMP_EXT, write, verbose);
      free(TB);
    } else {
      compressed_size = ctb.compressed_filesize;
    }
    TimePoint tt1 = now();
    total_compressed_size += compressed_size;
    if (verbose) {
      std::cout << filename << " size: " << size;
      std::cout << " compressed size: " << compressed_size << " (" << (double) compressed_size / size << ")";
      std::cout << std::endl;
      std::cout << count << ". Finished in " << (double) (tt1 - tt0) / 1000 << " s" << std::endl;
    }
  }
  TimePoint t1 = now();

  std::cout << "Compressed " << count << " files" << std::endl;
  std::cout << "TOTAL" << " size: " << total_size;
  std::cout << " compressed size: " << total_compressed_size << " (" << (double) total_compressed_size / total_size << ")" << std::endl;
  std::cout << "Finished in " << (double) (t1 - t0) / 1000 << " s" << std::endl;


  if (bench) {
    CompressedTB** ctbs = (CompressedTB**) malloc(count * sizeof(CompressedTB*));
    for (uint64_t i = 0; i < count; i++) {
      ctbs[i] = new CompressedTB(i, files[i]);
      // int16_t* decompressed_TB = (int16_t*) malloc(ctbs[i]->num_pos * sizeof(int16_t));
      // ctbs[i]->decompress_to_array(n_threads, decompressed_TB); // performs checksum test
      // free(decompressed_TB);
    }
    
    int n = 100000;
    t0 = now();
    for (int i = 0; i < n; i++) {
      int fix = rand() % count;
      CompressedTB* ctb = ctbs[fix];
      uint64_t ix = rand() % ctb->num_pos;
      ctb->get_value(ix);
    }
    t1 = now();
    std::cout << "Mean access time " << (double) (t1 - t0) / n << "ms vs filesize " << (double) total_compressed_size / (1024*1024*1024) <<  "GiB" << std::endl;
    
    for (uint64_t i = 0; i < count; i++) {
      free(ctbs[i]);
    }
    free(ctbs);
  }
}

/*
#include <openssl/md5.h>
void run_md5(std::string path) {
  for (const auto & entry : fs::recursive_directory_iterator(path)) {
    if (entry.path().extension() != COMP_EXT) continue;
    std::string bz_filename = entry.path().u8string();
    CompressedTB ctb = CompressedTB(0, bz_filename);
    std::string filename = bz_filename.substr(0, bz_filename.size() - 3);
    ctb.decompress_to_file(filename);
    run_cmd("md5sum " + filename);
    run_cmd("rm " + filename);
  }
}
*/

int main(int argc, char *argv[]) {
  std::string path = "egtbs";
 // [4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152]

  assert(argc == 4);
  uint64_t n_threads = atoi(argv[1]);
  uint64_t compression_level = atoi(argv[2]);
  uint64_t block_size = atoi(argv[3]);

  bool verbose = true;
  bool write = true;
  bool bench = true;


  recompress_files(path, n_threads, compression_level, block_size, verbose, write, bench);

  // run_md5(path);

}