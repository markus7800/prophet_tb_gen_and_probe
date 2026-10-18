#include <iostream>
#include <cassert>
#include <vector>
#include <algorithm>
#include "compressed_tb.h"
#include "egtb.h"
#include "kkx.h"
#include "triangular_indexes.h"
#include "misc.h"
#include <filesystem>
namespace fs = std::filesystem;

/*
Recompresses all .bz files in [source] with a different block size and writes them to [target] in the current
file format version, keeping the relative folder structure
(e.g. [source]/5men/0pawns/KQKRB.egtb.bz -> [target]/5men/0pawns/KQKRB.egtb.bz).
Files of an older format version (e.g. v1) are converted even if they already have the target block size.

Files that already exist in [target] are skipped if they are complete, have the current version and the target
block size and num_pos agree with the source file. If the source has the current version, the checksums have to
agree as well (checksums of different versions are not comparable; the conversion itself verifies them).
Otherwise they are recompressed and overwritten. With --overwrite, existing files are never skipped.

With --decompress, all .bz files in [source] are instead written uncompressed (FLAG_UNCOMPRESSED, see the .bz
format in compressed_tb.h) to [target] with the same names, so that prophet reads their values from the mmaped file
directly. The values are written in canonical index order, i.e. finalized files with a changed digit order are
permuted back. Capture don't-cares are kept (FLAG_CAPTURE_DC), prophet resolves them with a capture search.
Each table is decompressed in memory (2 bytes per position). Files that already exist in [target] are skipped if they
are complete, uncompressed and have the same num_pos as the source (unless --overwrite is given).
*/

// checks that an existing target file is complete, has the target block size and matches the source
bool target_is_valid(const fs::path& target_filename, const CompressedHeader& source, uint64_t block_size) {
  CompressedHeader target;
  if (!read_compressed_header(target_filename.u8string(), target)) return false;
  if (target.version != COMP_VERSION || (target.flags & FLAG_UNCOMPRESSED)) return false;
  if (source.version == target.version && source.checksum != target.checksum) return false;
  return target.block_size == block_size && target.num_pos == source.num_pos &&
    is_complete_compressed_file(target_filename.u8string());
}

void recompress(fs::path source, fs::path target, int n_threads, int compression_level, uint64_t block_size, bool overwrite) {
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

    CompressedHeader source_header;
    if (!read_compressed_header(source_filename.u8string(), source_header)) {
      std::cerr << "Invalid source file " << source_filename << std::endl;
      exit(1);
    }
    uint64_t source_size = fs::file_size(source_filename);
    total_source_size += source_size;

    if (!overwrite && fs::exists(target_filename) && target_is_valid(target_filename, source_header, block_size)) {
      std::cout << "Already exists." << std::endl;
      total_target_size += fs::file_size(target_filename);
      n_skipped++;
      continue;
    }

    fs::create_directories(target_filename.parent_path());
    TimePoint tt0 = now();

    uint64_t source_block_size = source_header.block_size;
    if (source_block_size == block_size && source_header.version == COMP_VERSION && !(source_header.flags & FLAG_UNCOMPRESSED)) {
      fs::copy_file(source_filename, target_filename, fs::copy_options::overwrite_existing);
      std::cout << "Copied (already block size " << block_size << ")." << std::endl;
      n_copied++;
    } else {
      recompress_TB(source_filename.u8string(), target_filename.u8string(), n_threads, compression_level, block_size, false);
      TimePoint tt1 = now();
      std::cout << "Recompressed v" << source_header.version << " " << source_block_size << " -> v" << COMP_VERSION << " " << block_size << ", ";
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

void decompress(fs::path source, fs::path target, int n_threads, bool overwrite) {
  std::cout << "source: " << source << ", target: " << target << ", n_threads: " << n_threads << std::endl;

  std::vector<fs::path> files;
  for (const auto & entry : fs::recursive_directory_iterator(source)) {
    if (entry.is_regular_file() && entry.path().extension() == COMP_EXT) files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  std::cout << "Found " << files.size() << " files." << std::endl;

  uint64_t n_skipped = 0, n_decompressed = 0, n_capture_dc = 0;
  uint64_t total_source_size = 0, total_target_size = 0;

  TimePoint t0 = now();
  for (uint64_t i = 0; i < files.size(); i++) {
    const fs::path& source_filename = files[i];
    fs::path target_filename = target / fs::relative(source_filename, source);
    std::cout << i+1 << "/" << files.size() << ". " << target_filename.u8string() << ": ";
    total_source_size += fs::file_size(source_filename);

    CompressedHeader source_header, target_header;
    if (!read_compressed_header(source_filename.u8string(), source_header)) {
      std::cerr << "Invalid source file " << source_filename << std::endl;
      exit(1);
    }
    if (!overwrite && fs::exists(target_filename) && read_compressed_header(target_filename.u8string(), target_header) &&
        (target_header.flags & FLAG_UNCOMPRESSED) && target_header.num_pos == source_header.num_pos &&
        is_complete_compressed_file(target_filename.u8string())) {
      std::cout << "Already exists." << std::endl;
      total_target_size += fs::file_size(target_filename);
      n_skipped++;
      continue;
    }

    fs::create_directories(target_filename.parent_path());
    TimePoint tt0 = now();

    std::string filename = source_filename.filename().u8string();
    EGTB egtb(source_filename.parent_path().u8string(), filename.substr(0, filename.find_first_of(".")));
    egtb.load_egtb_from_compressed_file(n_threads); // verifies the checksum, values in canonical index order
    bool permuted = egtb.file_permuted;
    bool capture_dc = egtb.capture_dc;
    write_uncompressed_TB(egtb.TB, egtb.num_pos, egtb.CTB->block_size, capture_dc ? FLAG_CAPTURE_DC : 0, target_filename.u8string(), n_threads);
    egtb.free_tb();

    TimePoint tt1 = now();
    std::cout << "Decompressed " << fs::file_size(source_filename) << " -> " << fs::file_size(target_filename) << " bytes";
    if (permuted) std::cout << " (permuted to canonical index order)";
    std::cout << " in " << (double) (tt1 - tt0) / 1000 << " s." << std::endl;
    if (capture_dc) {
      std::cout << "Warning: " << egtb.id << " has capture don't-cares, they are kept in the uncompressed file." << std::endl;
      n_capture_dc++;
    }
    total_target_size += fs::file_size(target_filename);
    n_decompressed++;
  }
  TimePoint t1 = now();

  std::cout << "Decompressed " << n_decompressed << ", skipped " << n_skipped << " files." << std::endl;
  if (n_capture_dc > 0) {
    std::cout << "Warning: " << n_capture_dc << " decompressed files have capture don't-cares (probes do a capture search, generation cannot use them)." << std::endl;
  }
  std::cout << "Total size: " << (double) total_source_size / (1024*1024*1024) << " GiB -> " << (double) total_target_size / (1024*1024*1024) << " GiB" << std::endl;
  std::cout << "Finished in " << (double) (t1 - t0) / 1000 << " s" << std::endl;
}

int main(int argc, char *argv[]) {
  bool decompress_mode = false;
  bool overwrite = false;
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--decompress") decompress_mode = true;
    else if (arg == "--overwrite") overwrite = true;
    else args.push_back(arg);
  }

  if (decompress_mode ? args.size() != 3 : (args.size() != 4 && args.size() != 5)) {
    std::cerr << "Usage: " << argv[0] << " [source] [target] [threads] [block_size] [compression_level=19] [--overwrite]" << std::endl;
    std::cerr << "       " << argv[0] << " --decompress [source] [target] [threads] [--overwrite]" << std::endl;
    return 1;
  }
  fs::path source = args[0];
  fs::path target = args[1];
  int n_threads = atoi(args[2].c_str());

  #ifndef OMP
  assert(n_threads == 1);
  #endif

  if (!fs::is_directory(source)) {
    std::cerr << "Source " << source << " is not a directory." << std::endl;
    return 1;
  }
  if (fs::exists(target) && fs::equivalent(source, target)) {
    std::cerr << "Source and target must be different directories." << std::endl;
    return 1;
  }

  if (decompress_mode) {
    Bitboards::init();
    init_kkx_table();
    init_kkp_table();
    init_tril();
    init_egtb_id_to_ix();
    decompress(source, target, n_threads, overwrite);
    return 0;
  }

  uint64_t block_size = strtoull(args[3].c_str(), NULL, 10);
  int compression_level = args.size() == 5 ? atoi(args[4].c_str()) : 19;
  assert(block_size > 0 && block_size % 8 == 0);
  assert(1 <= compression_level && compression_level <= 22);

  recompress(source, target, n_threads, compression_level, block_size, overwrite);
  return 0;
}
