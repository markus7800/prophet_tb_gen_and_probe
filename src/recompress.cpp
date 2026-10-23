#include <iostream>
#include <cassert>
#include <vector>
#include <algorithm>
#include "compressed_tb.h"
#include "misc.h"
#include <filesystem>
namespace fs = std::filesystem;

/*
Recompresses all .bz files in [source] with a different block size and writes them to [target],
keeping the relative folder structure (e.g. [source]/5men/0pawns/KQKRB.egtb.bz -> [target]/5men/0pawns/KQKRB.egtb.bz).

Files that already exist in [target] are skipped if they are complete, have the target block size and
num_pos / checksum agree with the source file. Otherwise they are recompressed and overwritten.
*/

// checks that an existing target file is complete, has the target block size and matches the source
bool target_is_valid(const fs::path& target_filename, uint64_t checksum, uint64_t num_pos, uint64_t block_size) {
  uint64_t target_checksum, target_num_pos, target_block_size;
  if (!read_compressed_header(target_filename.u8string(), target_checksum, target_num_pos, target_block_size)) return false;
  return target_block_size == block_size && target_num_pos == num_pos && target_checksum == checksum &&
    is_complete_compressed_file(target_filename.u8string());
}

void recompress(fs::path source, fs::path target, int n_threads, int compression_level, uint64_t block_size) {
  std::cout << "source: " << source << ", target: " << target << ", n_threads: " << n_threads;
  std::cout << ", compression_level: " << compression_level << ", block_size: " << block_size << std::endl;

  std::vector<fs::path> files;
  for (const auto & entry : fs::recursive_directory_iterator(source)) {
    if (entry.is_regular_file() && entry.path().extension() == COMP_EXT) files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  std::cout << "Found " << files.size() << " files." << std::endl;

  uint64_t n_skipped = 0, n_copied = 0, n_recompressed = 0;
  uint64_t total_source_size = 0, total_target_size = 0;

  TimePoint t0 = now();
  for (uint64_t i = 0; i < files.size(); i++) {
    const fs::path& source_filename = files[i];
    fs::path target_filename = target / fs::relative(source_filename, source);
    std::cout << i+1 << "/" << files.size() << ". " << target_filename.u8string() << ": ";

    uint64_t checksum, num_pos, source_block_size;
    if (!read_compressed_header(source_filename.u8string(), checksum, num_pos, source_block_size)) {
      std::cerr << "Invalid source file " << source_filename << std::endl;
      exit(1);
    }
    uint64_t source_size = fs::file_size(source_filename);
    total_source_size += source_size;

    if (fs::exists(target_filename) && target_is_valid(target_filename, checksum, num_pos, block_size)) {
      std::cout << "Already exists." << std::endl;
      total_target_size += fs::file_size(target_filename);
      n_skipped++;
      continue;
    }

    fs::create_directories(target_filename.parent_path());
    TimePoint tt0 = now();

    if (source_block_size == block_size) {
      fs::copy_file(source_filename, target_filename, fs::copy_options::overwrite_existing);
      std::cout << "Copied (already block size " << block_size << ")." << std::endl;
      n_copied++;
    } else {
      recompress_TB(source_filename.u8string(), target_filename.u8string(), n_threads, compression_level, block_size, false);
      TimePoint tt1 = now();
      std::cout << "Recompressed " << source_block_size << " -> " << block_size << ", ";
      std::cout << source_size << " -> " << fs::file_size(target_filename) << " bytes";
      std::cout << " in " << (double) (tt1 - tt0) / 1000 << " s." << std::endl;
      n_recompressed++;
    }
    total_target_size += fs::file_size(target_filename);
  }
  TimePoint t1 = now();

  std::cout << "Recompressed " << n_recompressed << ", copied " << n_copied << ", skipped " << n_skipped << " files." << std::endl;
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
  assert(block_size > 0 && block_size % 8 == 0);
  assert(1 <= compression_level && compression_level <= 22);

  if (!fs::is_directory(source)) {
    std::cerr << "Source " << source << " is not a directory." << std::endl;
    return 1;
  }
  if (fs::exists(target) && fs::equivalent(source, target)) {
    std::cerr << "Source and target must be different directories." << std::endl;
    return 1;
  }

  recompress(source, target, n_threads, compression_level, block_size);
  return 0;
}
