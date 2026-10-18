
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
  bool corrupt = false;

  #pragma omp parallel num_threads(nthreads) reduction(||:corrupt)
  {
    DecompressCtx* dctx = new DecompressCtx(block_size);
    uint64_t local_s = 0;

    #pragma omp for schedule(static)
    for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += block_size) {
      uint64_t block_ix = start_ix / block_size;
      uint64_t block_npos = get_block_npos(block_ix);
      if (!decompress_block(block_ix, dctx, dctx->uncompressed_buf)) {
        corrupt = true;
        continue;
      }

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

  return !corrupt && (s == checksum);
}

void build_value_dict(const int16_t* TB, uint64_t num_pos, [[maybe_unused]] int nthreads, ValueDict& dict) {
  std::vector<uint64_t> counts(65536, 0);
  #pragma omp parallel num_threads(nthreads)
  {
    std::vector<uint64_t> local_counts(65536, 0);
    #pragma omp for schedule(static)
    for (uint64_t ix = 0; ix < num_pos; ix++) {
      local_counts[(uint16_t) TB[ix]]++;
    }
    #pragma omp critical
    {
      for (int v = 0; v < 65536; v++) counts[v] += local_counts[v];
    }
  }

  value_dict_from_counts(counts.data(), dict);
}

void value_dict_from_counts(const uint64_t* counts, ValueDict& dict) {
  // most frequent first, ties broken by value to be deterministic
  std::vector<std::pair<uint64_t, int16_t>> freq;
  for (int v = 0; v < 65536; v++) {
    if (counts[v] > 0) freq.push_back({counts[v], (int16_t) (uint16_t) v});
  }
  std::sort(freq.begin(), freq.end(), [](const auto& a, const auto& b) {
    return a.first != b.first ? a.first > b.first : a.second < b.second;
  });

  dict.escape = freq.size() > EGTB_MAX_DICT;
  dict.size = dict.escape ? EGTB_MAX_DICT - 1 : freq.size();
  for (uint64_t i = 0; i < 256; i++) {
    dict.values[i] = i < dict.size ? freq[i].second : 0;
  }
}

uint64_t encode_block(const int16_t* values, uint64_t n, const int16_t* code_of, uint8_t escape_code, uint8_t* payload) {
  uint8_t* exceptions = payload + n;
  for (uint64_t i = 0; i < n; i++) {
    int16_t code = code_of[(uint16_t) values[i]];
    if (code >= 0) {
      payload[i] = (uint8_t) code;
    } else {
      payload[i] = escape_code;
      memcpy(exceptions, &values[i], sizeof(int16_t));
      exceptions += sizeof(int16_t);
    }
  }
  return exceptions - payload;
}

bool decode_block(const uint8_t* payload, uint64_t payload_size, uint64_t n, const ValueDict& dict, int16_t* values) {
  if (payload_size < n) return false;
  const uint8_t* exceptions = payload + n;
  const uint8_t* end = payload + payload_size;
  for (uint64_t i = 0; i < n; i++) {
    uint8_t code = payload[i];
    if (code < dict.size) {
      values[i] = dict.values[code];
    } else if (dict.escape && code == dict.size && exceptions + sizeof(int16_t) <= end) {
      memcpy(&values[i], exceptions, sizeof(int16_t));
      exceptions += sizeof(int16_t);
    } else {
      return false;
    }
  }
  return exceptions == end;
}

uint64_t block_compress_TB(int16_t* TB, uint64_t num_pos, int nthreads, int compression_level, uint64_t block_size, std::string compressed_filename, bool write, bool verbose, uint64_t digit_order, uint64_t extra_flags) {
  // about 2^20 values per chunk: large enough for few ordered writes, small enough for small per-thread buffers
  uint64_t chunk_size = block_size * std::max((uint64_t) 1, (uint64_t) (1 << 20) / block_size);
  auto fill = [TB](uint64_t start_ix, uint64_t n, int16_t* out) {
    memcpy(out, &TB[start_ix], n * sizeof(int16_t));
  };
  return block_compress_stream(num_pos, chunk_size, fill, nthreads, compression_level, block_size, compressed_filename, write, verbose, digit_order, extra_flags);
}

uint64_t block_compress_stream(uint64_t num_pos, uint64_t chunk_size, const ValueFill& fill, int nthreads, int compression_level, uint64_t block_size, std::string compressed_filename, bool write, bool verbose, uint64_t digit_order, uint64_t extra_flags) {
  assert (chunk_size % block_size == 0);
  uint64_t tb_size_bytes = num_pos * sizeof(int16_t);
  uint64_t n_chunks = (num_pos + chunk_size - 1) / chunk_size;
  uint64_t n_blocks = (num_pos + block_size - 1) / block_size;
  uint64_t blocks_per_chunk = chunk_size / block_size;

  // pass 1: value frequencies (for the dictionary) and checksum
  // every thread counts into its own histogram (65536 counters) which are added at the end; the checksum is a sum
  // over all indices (see checksum_term), so it does not depend on which thread processes which chunk
  std::vector<uint64_t> counts(65536, 0);
  uint64_t checksum = 0;
  #pragma omp parallel num_threads(nthreads) reduction(+:checksum)
  {
    std::vector<int16_t> values(chunk_size);
    std::vector<uint64_t> local_counts(65536, 0);
    // dynamic: the time of fill may vary between chunks
    #pragma omp for schedule(dynamic, 1)
    for (uint64_t chunk_ix = 0; chunk_ix < n_chunks; chunk_ix++) {
      uint64_t start_ix = chunk_ix * chunk_size;
      uint64_t n = std::min(chunk_size, num_pos - start_ix);
      fill(start_ix, n, values.data());
      for (uint64_t i = 0; i < n; i++) {
        local_counts[(uint16_t) values[i]]++;
        checksum += checksum_term(start_ix + i, values[i]);
      }
    }
    #pragma omp critical
    {
      for (int v = 0; v < 65536; v++) counts[v] += local_counts[v];
    }
  }

  // global value dictionary (most frequent values first) and the code of every value (-1: escaped)
  ValueDict dict;
  value_dict_from_counts(counts.data(), dict);
  std::vector<int16_t> code_of(65536, -1);
  for (uint64_t i = 0; i < dict.size; i++) code_of[(uint16_t) dict.values[i]] = i;
  uint8_t escape_code = dict.size; // only used if dict.escape

  // sizes of the file sections (see the .bz format in compressed_tb.h): the block size table has one UINT16 per block,
  // or UINT32 if a block has more than 65535 values (then a compressed block may be larger than 65535 bytes)
  bool large_blocks = block_size > uint64_t(UINT16_MAX);
  uint64_t max_compressed_block_size = large_blocks ? UINT32_MAX : UINT16_MAX;
  uint64_t block_sizes_bytes = (large_blocks ? sizeof(uint32_t) : sizeof(uint16_t)) * n_blocks;
  uint64_t dict_bytes = (dict.size * sizeof(int16_t) + 7) / 8 * 8;
  uint64_t header_size = COMP_V2_HEADER_SIZE + sizeof(uint64_t) + dict_bytes + (digit_order != 0 ? sizeof(uint64_t) : 0);

  // header and dictionary; the space of the block size table is skipped, because the compressed sizes are only known
  // after pass 2, then the blocks are appended
  FILE* f = NULL;
  if (write) {
    f = fopen((compressed_filename + ".tmp").c_str(), "wb");
    if (f == NULL) {
      std::cerr << "Error opening file: " << compressed_filename << std::endl;
      exit(1);
    }
    uint64_t flags = FLAG_RANK_ENCODED | (dict.escape ? FLAG_ESCAPE : 0) | (digit_order != 0 ? FLAG_DIGIT_ORDER : 0) | extra_flags;
    uint64_t header[7] = {COMP_MAGIC, COMP_VERSION, header_size, checksum, num_pos, block_size, flags};
    static_assert(sizeof(header) == COMP_V2_HEADER_SIZE);
    fwrite(header, sizeof(uint64_t), 7, f);
    fwrite(&dict.size, sizeof(uint64_t), 1, f);
    std::vector<uint8_t> dict_data(dict_bytes, 0);
    memcpy(dict_data.data(), dict.values, dict.size * sizeof(int16_t));
    fwrite(dict_data.data(), 1, dict_bytes, f);
    if (digit_order != 0) fwrite(&digit_order, sizeof(uint64_t), 1, f);
    fseek(f, header_size + block_sizes_bytes, SEEK_SET);
  }

  // pass 2: encode and compress; chunks are compressed in parallel and written in index order
  std::vector<uint64_t> block_sizes(n_blocks);
  uint64_t final_size = 0;
  #pragma omp parallel num_threads(nthreads)
  {
    // per-thread buffers: values of a chunk, payload of one block (codes + escaped values, at most 3 bytes per value),
    // and all compressed blocks of the chunk (worst case each block needs ZSTD_compressBound of its payload)
    std::vector<int16_t> values(chunk_size);
    std::vector<uint8_t> payload(3 * block_size);
    uint64_t max_block_bytes = ZSTD_compressBound(3 * block_size);
    std::vector<uint8_t> compressed(blocks_per_chunk * max_block_bytes);
    ZSTD_CCtx* cctx = ZSTD_createCCtx(); // reused for all blocks of this thread

    // static, 1: chunk i goes to thread i % nthreads, so the chunks that are written next are always being compressed
    // and a thread waits in the ordered section at most for the chunks of the other threads in the current round
    #pragma omp for ordered schedule(static, 1) reduction(+:final_size)
    for (uint64_t chunk_ix = 0; chunk_ix < n_chunks; chunk_ix++) {
      uint64_t chunk_start_ix = chunk_ix * chunk_size;
      uint64_t chunk_n = std::min(chunk_size, num_pos - chunk_start_ix);
      fill(chunk_start_ix, chunk_n, values.data());

      // blocks of the chunk are compressed one after the other into the chunk buffer
      uint64_t compressed_bytes = 0;
      for (uint64_t offset = 0; offset < chunk_n; offset += block_size) {
        uint64_t block_ix = (chunk_start_ix + offset) / block_size;
        uint64_t n = std::min(block_size, chunk_n - offset);
        uint64_t payload_size = encode_block(&values[offset], n, code_of.data(), escape_code, payload.data());
        uint64_t compressed_size = ZSTD_compressCCtx(cctx, &compressed[compressed_bytes], max_block_bytes, payload.data(), payload_size, compression_level);

        if (ZSTD_isError(compressed_size) || compressed_size == 0) {
          std::cerr << "Compression of block " << block_ix << " failed: " << ZSTD_getErrorName(compressed_size) << std::endl;
          exit(1);
        }
        if (compressed_size > max_compressed_block_size) {
          std::cerr << "Compressed size " << compressed_size << " of block " << block_ix << " does not fit into the block size table (max " << max_compressed_block_size << "). ";
          std::cerr << "Use a block size of at most " << UINT16_MAX / 2 << " or larger than " << UINT16_MAX << "." << std::endl;
          exit(1);
        }
        block_sizes[block_ix] = compressed_size;
        compressed_bytes += compressed_size;
      }
      final_size += compressed_bytes;

      // only one thread at a time, in chunk order: the blocks end up in index order in the file
      #pragma omp ordered
      {
        if (write) fwrite(compressed.data(), 1, compressed_bytes, f);
      }
    }
    ZSTD_freeCCtx(cctx);
  }

  final_size += block_sizes_bytes + header_size;

  if (write) {
    // fill in the block size table, then move the complete file to its final name
    fseek(f, header_size, SEEK_SET);
    if (large_blocks) {
      std::vector<uint32_t> write_block_sizes(block_sizes.begin(), block_sizes.end());
      fwrite(write_block_sizes.data(), sizeof(uint32_t), n_blocks, f);
    } else {
      std::vector<uint16_t> write_block_sizes(block_sizes.begin(), block_sizes.end());
      fwrite(write_block_sizes.data(), sizeof(uint16_t), n_blocks, f);
    }
    if (fclose(f) != 0) {
      std::cerr << "Error writing file: " << compressed_filename << std::endl;
      exit(1);
    }
    system(("mv " + compressed_filename + ".tmp " + compressed_filename).c_str());
    if (verbose) std::cout << "Wrote to " << compressed_filename << std::endl;
  }

  if (verbose) std::cout << "Compressed final size: " << final_size << " ratio: " << (double) final_size / tb_size_bytes
    << " (dictionary: " << dict.size << " values" << (dict.escape ? " + escape" : "") << ")" << std::endl;

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

bool parse_value_dict(const uint8_t* data, uint64_t n, const CompressedHeader& header, ValueDict& dict) {
  dict.size = 0;
  dict.escape = false;
  for (int i = 0; i < 256; i++) dict.values[i] = 0;
  if (!(header.flags & FLAG_RANK_ENCODED)) return true;
  if (header.version < 2 || n < COMP_V2_HEADER_SIZE + sizeof(uint64_t)) return false;
  uint64_t size;
  memcpy(&size, data + COMP_V2_HEADER_SIZE, sizeof(uint64_t));
  dict.escape = header.flags & FLAG_ESCAPE;
  if (size > 256 || (dict.escape && size > 255) || size == 0) return false;
  if (header.header_size < COMP_V2_HEADER_SIZE + sizeof(uint64_t) + size * sizeof(int16_t) || n < header.header_size) return false;
  dict.size = size;
  memcpy(dict.values, data + COMP_V2_HEADER_SIZE + sizeof(uint64_t), size * sizeof(int16_t));
  return true;
}

bool parse_digit_order(const uint8_t* data, uint64_t n, const CompressedHeader& header, uint64_t& digit_order) {
  digit_order = 0;
  if (!(header.flags & FLAG_DIGIT_ORDER)) return true;
  if (!(header.flags & FLAG_RANK_ENCODED)) return false;
  uint64_t dict_size;
  memcpy(&dict_size, data + COMP_V2_HEADER_SIZE, sizeof(uint64_t));
  uint64_t offset = COMP_V2_HEADER_SIZE + sizeof(uint64_t) + (dict_size * sizeof(int16_t) + 7) / 8 * 8;
  if (header.header_size < offset + sizeof(uint64_t) || n < offset + sizeof(uint64_t)) return false;
  memcpy(&digit_order, data + offset, sizeof(uint64_t));
  return digit_order != 0;
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

  if (header.flags & FLAG_UNCOMPRESSED) return filesize == header.header_size + num_pos * sizeof(int16_t);

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

void write_uncompressed_TB(const int16_t* TB, uint64_t num_pos, uint64_t block_size, uint64_t extra_flags, std::string filename, int nthreads) {
  uint64_t checksum = compute_checksum(TB, num_pos, nthreads);
  uint64_t flags = FLAG_UNCOMPRESSED | extra_flags;
  uint64_t header[7] = {COMP_MAGIC, COMP_VERSION, COMP_V2_HEADER_SIZE, checksum, num_pos, block_size, flags};
  static_assert(sizeof(header) == COMP_V2_HEADER_SIZE);
  std::string tmp_filename = filename + ".tmp";
  FILE* f = fopen(tmp_filename.c_str(), "wb");
  if (f == NULL || fwrite(header, sizeof(uint64_t), 7, f) != 7 || fwrite(TB, sizeof(int16_t), num_pos, f) != num_pos || fclose(f) != 0) {
    std::cerr << "Could not write " << tmp_filename << std::endl;
    exit(1);
  }
  if (rename(tmp_filename.c_str(), filename.c_str()) != 0) {
    std::cerr << "Could not rename " << tmp_filename << " to " << filename << std::endl;
    exit(1);
  }
}

void recompress_TB(std::string source_filename, std::string target_filename, int nthreads, int compression_level, uint64_t block_size, bool verbose) {
  uint64_t old_checksum, num_pos, old_block_size;
  if (!read_compressed_header(source_filename, old_checksum, num_pos, old_block_size) || !is_complete_compressed_file(source_filename)) {
    std::cerr << "Invalid compressed file " << source_filename << std::endl;
    exit(1);
  }

  uint64_t digit_order = 0;
  uint64_t extra_flags = 0;
  int16_t* TB = (int16_t*) malloc(num_pos * sizeof(int16_t));
  if (TB == NULL) {
    std::cerr << "Could not allocate " << num_pos * sizeof(int16_t) << " bytes." << std::endl;
    exit(1);
  }
  {
    // unmap source before writing, source may be equal to target
    CompressedTB ctb = CompressedTB(0, source_filename);
    ctb.decompress_to_array(nthreads, TB); // exits if the checksum of the source does not match
    digit_order = ctb.digit_order; // the values are kept in the order of the file
    extra_flags = ctb.flags & FLAG_CAPTURE_DC;
  }
  // the target is always written in the current version, whose checksum may differ from the source's
  uint64_t checksum = compute_checksum(TB, num_pos, nthreads);
  // writes to target_filename.tmp first and then moves it, so an interruption does not leave a partial file
  block_compress_TB(TB, num_pos, nthreads, compression_level, block_size, target_filename, true, verbose, digit_order, extra_flags);
  free(TB);

  uint64_t new_checksum, new_num_pos, new_block_size;
  if (!read_compressed_header(target_filename, new_checksum, new_num_pos, new_block_size) || !is_complete_compressed_file(target_filename) ||
      new_checksum != checksum || new_num_pos != num_pos || new_block_size != block_size) {
    std::cerr << "Recompressed file " << target_filename << " is invalid." << std::endl;
    exit(1);
  }
}
