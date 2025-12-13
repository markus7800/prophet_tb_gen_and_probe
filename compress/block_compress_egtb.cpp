#include "repair.h"
#include "huffman.h"
#include "encoder.h"
#include <omp.h>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <vector>
namespace fs = std::filesystem;
#include "libdeflate.h"

// #define BLOCKSIZE uint64_t(33554432)
// #define BLOCKSIZE uint64_t(1048576)
// #define BLOCKSIZE uint64_t(60000)
// #define BLOCKSIZE uint64_t(32768)
#define BLOCKSIZE uint64_t(8192)
// #define BLOCKSIZE uint64_t(1024)


uint64_t compress_block(int16_t* TB_BLOCK, int16_t min_nonzero_val, uint64_t size_w, uint64_t freq_local[], RDS* rds) {

  uint16_t CHAR_SIZE = 1000 - min_nonzero_val + 1 + 1; // [minval,...,1000] + 0

  rds->txt_len = size_w;
  rds->num_pairs = 0;

  // init sequence
  SEQ* seq = rds->seq;
  for (uint64_t i = 0; i < size_w; i++) {
    if (TB_BLOCK[i] == 0) {
      seq[i].code = 0;
    } else {
      seq[i].code = abs(TB_BLOCK[i]) - min_nonzero_val + 1;
    }
    seq[i].next = DUMMY_POS;
    seq[i].prev = DUMMY_POS;
  }

  // init hashtable
  PAIR** h_first = rds->h_first;
  uint64_t h_num = rds->h_num;
  for (uint64_t i = 0; i < primes[h_num]; i++) {
    h_first[i] = NULL;
  }
    
  // init priority queue
  uint64_t p_max = rds->p_max;
  PAIR** p_que = rds->p_que;
  for (uint64_t i = 0; i < p_max; i++) {
    p_que[i] = NULL;
  }

  // count initial pair frequencies
  initRDS_by_counting_pairs(rds);
    

  // repair
  DICT* dict = createDict(rds->txt_len, CHAR_SIZE);
    
  uint64_t num_rules = 0;
  PAIR* max_pair;
  CODE new_code = 0;
  while ((max_pair = getMaxPair(rds)) != NULL) {
    num_rules++;
    new_code = addNewPair(dict, max_pair);
    replacePairs(rds, max_pair, new_code);
  }
  assert(rds->num_pairs == 0);

  // encoding
  getCompSeq(rds, dict);
  
  EDICT* edict = convertDict(dict, CHAR_SIZE);
  uint64_t cfg_bits = EncodeCFG(edict, CHAR_SIZE);
    
  // global huff
  for (uint64_t i = 0; i < dict->seq_len; i++) freq_local[dict->comp_seq[i]]++;
  
  DestructEDict(edict);

  return (cfg_bits / 8 + 1);
}

void compress_egtb(const char* filename, int nthreads) {

  // load file
  FILE *f = fopen(filename, "rb");
  if (f == NULL) {
    std::cerr << "Error opening file: " << filename << std::endl;
    return;
  }
  fseek(f, 0, SEEK_END);
  uint64_t file_size = ftell(f);
  rewind(f);
  
  uint64_t num_pos = file_size / sizeof(int16_t);
  int16_t *TB = (int16_t*) malloc(num_pos * sizeof(int16_t));
  
  fread(TB, sizeof(int16_t), num_pos, f);
  fclose(f);


  // determine initial alphabet size
  uint16_t min_nonzero_val = 1000;
  #pragma omp parallel for num_threads(nthreads) schedule(static) reduction(min:min_nonzero_val)
  for (uint64_t ix = 0; ix < num_pos; ix++) {
    // 0 -> draw
    // negative even numbers -> loss in ply
    // positive odd numbers -> win in ply
    if (TB[ix] != 0) min_nonzero_val = std::min(min_nonzero_val, (uint16_t) abs(TB[ix]));
  }
  uint16_t CHAR_SIZE = 1000 - min_nonzero_val + 1 + 1; // [minval,...,1000] + 0
  std::cout << "Read " << num_pos << " entries (" << (double) num_pos*2 / (1024*1024*1024) << " GiB) from " << filename << " -> min_nonzero_val=" << min_nonzero_val << " and CHAR_SIZE=" << CHAR_SIZE << std::endl;
  
  uint64_t n_blocks = ceil((double) num_pos / BLOCKSIZE);
  
  uint64_t cfg_bits = 0;
  uint64_t freq[UINT16_MAX] = {0};
  uint64_t lengths[UINT16_MAX] = {0};

  #pragma omp parallel num_threads(nthreads)
  {
    // thread-private allocations

    uint64_t freq_local[UINT16_MAX] = {0}; // frequency count for global huffman encoding
    RDS* rds = (RDS*) malloc(sizeof(RDS));
    rds->seq = (SEQ*) malloc(BLOCKSIZE * sizeof(SEQ));

    uint64_t h_num = INIT_HASH_NUM;
    rds->h_num = h_num;
    rds->h_first = (PAIR**) malloc(sizeof(PAIR*) * primes[h_num]);

    uint64_t p_max = (uint64_t) ceil(sqrt((double) BLOCKSIZE));
    rds->p_max = p_max;
    rds->p_que = (PAIR**) malloc(sizeof(PAIR*) * p_max);;

    #pragma omp for schedule(static) reduction(+:cfg_bits)
    for (uint64_t start_ix = 0; start_ix < num_pos; start_ix += BLOCKSIZE) {
      uint64_t size_w = std::min(BLOCKSIZE, num_pos - start_ix);
      cfg_bits += compress_block(&TB[start_ix], min_nonzero_val, size_w, freq_local, rds);
    }

    #pragma omp critical
    {
      for (int i = 0; i < UINT16_MAX; i++) {
        freq[i] += freq_local[i];
      }
    }

    destructRDS(rds);
  }

  uint64_t final_size = cfg_bits;

  Node* tree = buildTree(freq, UINT16_MAX);
  extractLengths(tree, 0, lengths);
  
  uint64_t huffcoded_seq_bits = 0;
  uint64_t nonzero_symbols = 0;
  for (CODE code = 0; code < UINT16_MAX; code++) {
    huffcoded_seq_bits += freq[code] * lengths[code];
    nonzero_symbols += (freq[code] > 0);
  }
  final_size += (huffcoded_seq_bits / 8) + 1;
  final_size += nonzero_symbols * 4; // this is upper bound
  final_size += n_blocks * 2; // jump table
  
  delete tree;
  std::cout << "final size: " << final_size << " " << (double) final_size / file_size << std::endl;

  free(TB);
}

uint64_t compress_egtb_old(const char* filename) {
  uint64_t batchsize = BLOCKSIZE;

   FILE *f = fopen(filename, "rb");
  if (f == NULL) {
      std::cerr << "Error opening file: " << filename << std::endl;
      return 0;
  }
  fseek(f, 0, SEEK_END);
  long bytes = ftell(f);
  rewind(f);

  size_t count = bytes / sizeof(int16_t);
  int16_t *TB = (int16_t*) malloc(count * sizeof(int16_t));

  fread(TB, sizeof(int16_t), count, f);
  fclose(f);

  uint16_t min_val = 1000;
  for (uint64_t ix = 0; ix < count; ix++) {
    if (TB[ix] != 0)
      min_val = std::min(min_val, (uint16_t) abs(TB[ix]));
  }
  uint16_t CHAR_SIZE = 1000 - min_val + 1 + 1;
  std::cout << "Read " << count << " entries from " << filename << " -> min_val=" << min_val << " and CHAR_SIZE=" << CHAR_SIZE << std::endl;

  // global huff
  uint64_t* freq = (uint64_t*) calloc(UINT16_MAX, sizeof(uint64_t));
  uint64_t* lengths = (uint64_t*) calloc(UINT16_MAX, sizeof(uint64_t));

  uint64_t final_size = 0;
  double avg_num_rules = 0.0;
  double avg_seq_len = 0.0;
  double avg_cfg_bytes = 0.0;
  uint64_t num_replace_pairs = 0;
  uint64_t n_batches = ceil((double)count/batchsize);
  for (uint64_t start = 0; start < count; start += batchsize) {
    
    uint64_t size_w = std::min(batchsize, count - start);

    // init sequence
    SEQ* seq = (SEQ*) malloc(size_w * sizeof(SEQ));
    for (uint64_t i = 0; i < size_w; i++) {
      if (TB[start + i] == 0) {
        seq[i].code = 0;
      } else {
        seq[i].code = abs(TB[start + i]) - min_val + 1;
      }
      seq[i].next = DUMMY_POS;
      seq[i].prev = DUMMY_POS;
    }

    // init hash table
    uint64_t h_num = INIT_HASH_NUM;
    Pair** h_first = (PAIR**) malloc(sizeof(PAIR*) * primes[h_num]);
    for (uint64_t i = 0; i < primes[h_num]; i++) {
        h_first[i] = NULL;
    }

    // init priority queue
    uint64_t p_max = (uint64_t) ceil(sqrt((double) size_w));
    PAIR** p_que = (PAIR**) malloc(sizeof(PAIR*) * p_max);
    for (uint64_t i = 0; i < p_max; i++) {
        p_que[i] = NULL;
    }
    
    // populate RDS object
    RDS* rds = (RDS*) malloc(sizeof(RDS));
    rds->txt_len = size_w;
    rds->seq = seq;
    rds->num_pairs = 0;
    rds->h_num = h_num;
    rds->h_first = h_first;
    rds->p_max = p_max;
    rds->p_que = p_que;

    initRDS_by_counting_pairs(rds);


    DICT* dict = createDict(rds->txt_len, CHAR_SIZE);

    // CODE zero_code = 0;
    // PAIR* zero_pair;
    // for (int i = 0; i < 5; i++) {
    //   if ((zero_pair = findPair(rds, zero_code, zero_code)) != NULL) {
    //     if (zero_pair->freq > 1) {
    //       zero_code = addNewPair(dict, zero_pair);;
    //       replacePairs(rds, zero_pair, zero_code);
    //     }
    //   }
    // }


    uint64_t num_rules = 0;
    uint64_t num_replaced = 0;
    PAIR* max_pair;
    CODE new_code = 0;
    while ((max_pair = getMaxPair(rds)) != NULL) {
      // if (num_rules == 0) std::cout << "max_pair: (" << max_pair->left << "," << max_pair->right << ")\n";
      freq[max_pair->left]++;
      freq[max_pair->right]++;
      // freq[max_pair->left & 0xff]++;
      // freq[(max_pair->left >> 8) & 0xff]++;
      // freq[max_pair->right & 0xff]++;
      // freq[(max_pair->right >> 8) & 0xff]++;
      num_rules++;
      new_code = addNewPair(dict, max_pair);
      num_replaced += replacePairs(rds, max_pair, new_code);
      num_replace_pairs++;
    }
    // std::cout << "num_pairs: " << rds->num_pairs << std::endl;
    assert(rds->num_pairs == 0);
    // CODE max_code = new_code;
    // std::cout << "Total number of replacements: " << num_replaced << std::endl;
    // std::cout << "Total number of rules: " << num_rules << std::endl;
    getCompSeq(rds, dict);
    // std::cout << "Compressed sequence length: " << dict->seq_len << std::endl;


    EDICT* edict = convertDict(dict, CHAR_SIZE); // deletes dict
    // uint64_t cfg_bits = EncodeCFG(edict, CHAR_SIZE);
    // uint64_t cfg_bits = 2 * num_rules * 16;
    uint64_t cfg_bits = 0;

    avg_num_rules += (double) num_rules / n_batches;
    avg_seq_len += (double) edict->seq_len / n_batches;
    avg_cfg_bytes += ceil((double) cfg_bits / 8) / n_batches;

    final_size += ceil((double) cfg_bits / 8);
    // final_size += 2*edict->seq_len; 

    // global huff
    for (uint64_t i = 0; i < edict->seq_len; i++) {
      freq[edict->comp_seq[i]]++;
      // freq[edict->comp_seq[i] & 0xff]++;
      // freq[(edict->comp_seq[i] >> 8) & 0xff]++;
    }

    DestructEDict(edict);
    destructRDS(rds);
  }

  // global huff
  Node* tree = buildTree(freq, UINT16_MAX);
  extractLengths(tree, 0, lengths);

  uint64_t huffcoded_seq_bits = 0;
  uint64_t nonzero_symbols = 0;
  for (CODE code = 0; code < UINT16_MAX; code++) {
    huffcoded_seq_bits += freq[code] * lengths[code];
    nonzero_symbols += (freq[code] > 0);
    // if (freq[code] > 0) std::cout << code << ": " << freq[code] << " " << lengths[code] << std::endl;
  }
  final_size += (huffcoded_seq_bits / 8) + 1;
  final_size += nonzero_symbols * 4; // this is upper bound
  final_size += n_batches * 2; // jump table

  free(freq);
  free(lengths);
  delete tree;


  std::cout << "final size: " << final_size << " " << (double) final_size / (count*2.0) << std::endl;
  std::cout << "nonzero_symbols: " << nonzero_symbols << ", n_batches: " << n_batches << std::endl;
  std::cout << "avg_num_rules: " << avg_num_rules << ", avg_seq_len: " << avg_seq_len << ", avg_cfg_bytes: " << avg_cfg_bytes << std::endl;
  free(TB);

  return final_size;
}


uint64_t compress_egtb_libdeflate(const char* filename) {

  FILE *f = fopen(filename, "rb");
  if (f == NULL) {
      std::cerr << "Error opening file: " << filename << std::endl;
      return 0;
  }
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  rewind(f);

  size_t num_pos = file_size / sizeof(int16_t);
  int16_t *TB = (int16_t*) malloc(num_pos * sizeof(int16_t));

  fread(TB, sizeof(int16_t), num_pos, f);
  fclose(f);

  uint64_t final_size = 0;
  std::vector<uint8_t> compressed(BLOCKSIZE*2);
  std::vector<uint8_t> decompressed(BLOCKSIZE*2);

  uint64_t n_blocks = ceil((double) num_pos / BLOCKSIZE);

  for (uint64_t start = 0; start < num_pos; start += BLOCKSIZE) {
    
    uint64_t size_w = std::min(BLOCKSIZE, num_pos - start);


    libdeflate_compressor* compressor = libdeflate_alloc_compressor(10);

    uint64_t compressed_size = libdeflate_deflate_compress(compressor, (uint8_t*) &TB[start], 2*size_w, compressed.data(), compressed.size());

    if (compressed_size == 0) {
      std::cerr << "Compression failed\n";
      return 1;
    }

    final_size += compressed_size;

#if 0
    uint16_t unused;
    libdeflate_free_compressor(compressor);

    libdeflate_decompressor* decompressor = libdeflate_alloc_decompressor();
    size_t actual_size;
    libdeflate_result res = libdeflate_deflate_decompress(decompressor, compressed.data(), compressed.size(), decompressed.data(), decompressed.size(), &actual_size);
    
    if (res != LIBDEFLATE_SUCCESS) {
      std::cerr << res << " Decompression failed\n";
      return 1;
    }

    libdeflate_free_decompressor(decompressor);
#endif

  }

  final_size += n_blocks * 2; // jump table
  std::cout << "final size: " << final_size << " " << (double) final_size / file_size << std::endl;
 
  free(TB);

  return final_size;
}



int main(int argc, char *argv[]) {
  
  // uint64_t total_size = 0;
  // uint64_t total_compressed_size = 0;
  // uint64_t total_zipped_size = 0;
  // std::string path = "5men/egtbs/";
  // for (const auto & entry : fs::directory_iterator(path)) {
  //   if (entry.path().extension() != ".egtb") continue;
  //   uint64_t size = entry.file_size();
  //   total_size += size;
  //   uint64_t zipped_size = fs::file_size(fs::path("5men/zipped/" + entry.path().filename().u8string() + ".zip"));
  //   total_zipped_size += zipped_size;
  //   uint64_t compressed_size = RunRepair(batchsize, entry.path().u8string().c_str());
  //   total_compressed_size += compressed_size;
  //   std::cout << entry.path() << " size: " << size;
  //   std::cout << " zipped size: " << zipped_size << " (" << (double) zipped_size / size << ")";
  //   std::cout << " compressed size: " << compressed_size << " (" << (double) compressed_size / size << ")";
  //   std::cout << (compressed_size <= zipped_size ? " OK" : " :(") << std::endl;
  //   std::cout << std::endl;
  // }
  // std::cout << "TOTAL" << " size: " << total_size << " zipped size: " << total_zipped_size << " (" << (double) total_zipped_size / total_size << ") compressed size: " << total_compressed_size << " (" << (double) total_compressed_size / total_size << ")" << std::endl;
  // exit(1);
  
  // compress_egtb("../egtbs/5men/1pawns/KRPKQ.egtb", 1);
  // compress_egtb_old("../egtbs/5men/1pawns/KRPKQ.egtb");

  const char* filename = "../egtbs/5men/1pawns/KRKRP.egtb";
  // compress_egtb_old(filename);
  compress_egtb_libdeflate(filename);

  return 0;
}