#include <iostream>
#include <cassert>
#include <vector>
#include <algorithm>
#include "compressed_tb.h"
#include "misc.h"
#include <filesystem>
namespace fs = std::filesystem;

/*
Converts all DTM files *.egtb.bz in [source] to WDL files *.wdl.bz in [target] with the given block size,
keeping the relative folder structure (e.g. [source]/5men/0pawns/KQKRB.egtb.bz -> [target]/5men/0pawns/KQKRB.wdl.bz).
The WDL file stores one bit per position: 1 if side-to-move wins, 0 otherwise.

Files that already exist in [target] are skipped if they are complete, have the target block size and
num_pos agrees with the source file. Otherwise they are converted and overwritten.
*/

// checks that an existing target file is complete, has the target block size and matches the source
bool target_is_valid(const fs::path& target_filename, uint64_t num_pos, uint64_t block_size) {
  uint64_t target_checksum, target_num_pos, target_block_size;
  if (!read_compressed_header(target_filename.u8string(), target_checksum, target_num_pos, target_block_size)) return false;
  return target_block_size == block_size && target_num_pos == num_pos &&
    is_complete_compressed_file(target_filename.u8string());
}

void pack_win_bits(int16_t* TB, uint64_t num_pos, uint8_t* WDL, [[maybe_unused]] int n_threads) {
  uint64_t num_bytes = wdl_num_bytes(num_pos);
  #pragma omp parallel for num_threads(n_threads) schedule(static)
  for (uint64_t byte_ix = 0; byte_ix < num_bytes; byte_ix++) {
    uint8_t byte = 0;
    for (uint64_t ix = byte_ix * 8; ix < std::min(byte_ix * 8 + 8, num_pos); ix++) {
      byte |= (TB[ix] > 0) << (ix % 8);
    }
    WDL[byte_ix] = byte;
  }
}

// compares every win bit of target file with source DTM values
bool verify_target(int16_t* TB, const fs::path& target_filename, int n_threads) {
  CompressedTB ctb = CompressedTB(0, target_filename.u8string(), true);
  if (!ctb.check_integrity(n_threads)) return false;
  uint8_t* WDL = (uint8_t*) malloc(ctb.get_uncompressed_size());
  ctb.decompress_to_bytes(n_threads, WDL);
  bool ok = true;
  #pragma omp parallel for num_threads(n_threads) schedule(static) reduction(&&:ok)
  for (uint64_t ix = 0; ix < ctb.num_pos; ix++) {
    ok = ok && (((WDL[ix / 8] >> (ix % 8)) & 1) == (TB[ix] > 0));
  }
  free(WDL);
  return ok;
}

void to_wdl(fs::path source, fs::path target, int n_threads, int compression_level, uint64_t block_size) {
  std::cout << "source: " << source << ", target: " << target << ", n_threads: " << n_threads;
  std::cout << ", compression_level: " << compression_level << ", block_size: " << block_size << std::endl;

  std::vector<fs::path> files;
  for (const auto & entry : fs::recursive_directory_iterator(source)) {
    if (entry.is_regular_file() && entry.path().extension() == COMP_EXT && entry.path().stem().extension() == DTM_EXT) files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  std::cout << "Found " << files.size() << " files." << std::endl;

  uint64_t n_skipped = 0, n_converted = 0;
  uint64_t total_source_size = 0, total_target_size = 0;

  TimePoint t0 = now();
  for (uint64_t i = 0; i < files.size(); i++) {
    const fs::path& source_filename = files[i];
    fs::path target_filename = target / fs::relative(source_filename, source);
    target_filename.replace_extension(); // .egtb.bz -> .egtb
    target_filename.replace_extension(std::string(WDL_EXT) + COMP_EXT); // .egtb -> .wdl.bz
    std::cout << i+1 << "/" << files.size() << ". " << target_filename.u8string() << ": ";

    uint64_t checksum, num_pos, source_block_size;
    if (!read_compressed_header(source_filename.u8string(), checksum, num_pos, source_block_size) || !is_complete_compressed_file(source_filename.u8string())) {
      std::cerr << "Invalid source file " << source_filename << std::endl;
      exit(1);
    }
    uint64_t source_size = fs::file_size(source_filename);
    total_source_size += source_size;

    if (fs::exists(target_filename) && target_is_valid(target_filename, num_pos, block_size)) {
      std::cout << "Already exists." << std::endl;
      total_target_size += fs::file_size(target_filename);
      n_skipped++;
      continue;
    }

    fs::create_directories(target_filename.parent_path());
    TimePoint tt0 = now();

    int16_t* TB = (int16_t*) malloc(num_pos * sizeof(int16_t));
    uint8_t* WDL = (uint8_t*) malloc(wdl_num_bytes(num_pos));
    if (TB == NULL || WDL == NULL) {
      std::cerr << "Could not allocate " << num_pos * sizeof(int16_t) + wdl_num_bytes(num_pos) << " bytes." << std::endl;
      exit(1);
    }
    {
      CompressedTB ctb = CompressedTB(0, source_filename.u8string());
      ctb.decompress_to_array(n_threads, TB);
      if (compute_checksum(TB, num_pos, n_threads) != checksum) {
        std::cerr << "Checksum mismatch in source file " << source_filename << std::endl;
        exit(1);
      }
    }
    pack_win_bits(TB, num_pos, WDL, n_threads);
    // writes to target_filename.tmp first and then moves it, so an interruption does not leave a partial file
    block_compress_WDL(WDL, num_pos, n_threads, compression_level, block_size, target_filename.u8string(), true, false);
    free(WDL);

    if (!target_is_valid(target_filename, num_pos, block_size) || !verify_target(TB, target_filename, n_threads)) {
      std::cerr << "Converted file " << target_filename << " is invalid." << std::endl;
      exit(1);
    }
    free(TB);

    TimePoint tt1 = now();
    std::cout << "Converted " << source_size << " -> " << fs::file_size(target_filename) << " bytes";
    std::cout << " in " << (double) (tt1 - tt0) / 1000 << " s." << std::endl;
    n_converted++;
    total_target_size += fs::file_size(target_filename);
  }
  TimePoint t1 = now();

  std::cout << "Converted " << n_converted << ", skipped " << n_skipped << " files." << std::endl;
  std::cout << "Total size: " << (double) total_source_size / (1024*1024*1024) << " GiB -> " << (double) total_target_size / (1024*1024*1024) << " GiB" << std::endl;
  std::cout << "Finished in " << (double) (t1 - t0) / 1000 << " s" << std::endl;
}

int main(int argc, char *argv[]) {
  if (argc != 5 && argc != 6) {
    std::cerr << "Usage: " << argv[0] << " [source] [target] [threads] [block_size] [compression_level=19]" << std::endl;
    return 1;
  }
  fs::path source = argv[1];
  fs::path target = argv[2];
  int n_threads = atoi(argv[3]);
  uint64_t block_size = strtoull(argv[4], NULL, 10);
  int compression_level = argc == 6 ? atoi(argv[5]) : 19;

  #ifndef OMP
  assert(n_threads == 1);
  #endif
  if (block_size == 0 || block_size % 8 != 0) {
    std::cerr << "Block size has to be positive and divisible by 8." << std::endl;
    return 1;
  }
  assert(1 <= compression_level && compression_level <= 22);

  if (!fs::is_directory(source)) {
    std::cerr << "Source " << source << " is not a directory." << std::endl;
    return 1;
  }

  to_wdl(source, target, n_threads, compression_level, block_size);
  return 0;
}
