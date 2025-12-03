
#include <iostream>
#include <assert.h>
#include "repair.h"
#include "encoder.h"

uint16_t leftPos_SQ(RDS *rds, uint16_t pos);
uint16_t rightPos_SQ(RDS *rds, uint16_t pos);
void removeLink_SQ(RDS *rds, uint16_t target_pos);
void updateBlock_SQ(RDS *rds, CODE new_code, uint16_t target_pos);
PAIR *locatePair(RDS *rds, CODE left, CODE right);
void growHashtable(RDS *rds);
void insertPair_PQ(RDS *rds, PAIR *target, uint64_t p_num);
void removePair_PQ(RDS *rds, PAIR *target, uint64_t p_num);
void incrementPair(RDS *rds, PAIR *target);
void decrementPair(RDS *rds, PAIR *target);
PAIR* getMaxPair(RDS *rds);
PAIR *createPair(RDS *rds, CODE left, CODE right, uint16_t f_pos);
void destructPair(RDS *rds, PAIR *target);
void resetPQ(RDS *rds, uint64_t p_num);
void initRDS_by_counting_pairs(RDS *rds);

// sequence

uint16_t leftPos_SQ(RDS *rds, uint16_t pos) {
  SEQ *seq = rds->seq;

  assert(pos != DUMMY_POS);
  if (pos == 0) {
    return DUMMY_POS;
  }

  if (seq[pos-1].code == DUMMY_CODE) {
    return seq[pos-1].next; // reference to next valid position
  } else {
    return pos-1;
  }
}

uint16_t rightPos_SQ(RDS *rds, uint16_t pos) {
  SEQ *seq = rds->seq;

  assert(pos != DUMMY_POS);
  if (pos == rds->txt_len-1) {
    return DUMMY_POS;
  }

  if (seq[pos+1].code == DUMMY_CODE) {
    return seq[pos+1].prev; // reference to next valid position
  } else {
    return pos+1;
  }
}

// remove links at target_pos (pair starting at target_pos) from the doubly-linked sequence of pairs
// does not update seq[target_pos].prev an seq[target_pos].next
void removeLink_SQ(RDS *rds, uint16_t target_pos) {
  SEQ *seq = rds->seq;
  assert(seq[target_pos].code != DUMMY_CODE);

  uint16_t prev_pos = seq[target_pos].prev;
  uint16_t next_pos = seq[target_pos].next;

  if (prev_pos != DUMMY_POS && next_pos != DUMMY_POS) {
    seq[prev_pos].next = next_pos;
    seq[next_pos].prev = prev_pos;
  }
  else if (prev_pos == DUMMY_POS && next_pos != DUMMY_POS) {
    seq[next_pos].prev = DUMMY_POS;
  }
  else if (prev_pos != DUMMY_POS && next_pos == DUMMY_POS) {
    seq[prev_pos].next = DUMMY_POS;
  }
}

void updateBlock_SQ(RDS *rds, CODE new_code, uint16_t target_pos) {
  SEQ *seq = rds->seq;
  PAIR *l_pair, *c_pair, *r_pair;


  uint16_t l_pos  = leftPos_SQ(rds, target_pos);
  uint16_t r_pos  = rightPos_SQ(rds, target_pos);
  uint16_t rr_pos = rightPos_SQ(rds, r_pos);
  CODE c_code = seq[target_pos].code;
  CODE r_code = seq[r_pos].code;
  assert(c_code != DUMMY_CODE);
  assert(r_code != DUMMY_CODE);

  // l_pos  ...  target_pos  ...  r_pos  ...  rr_pos
  // l_code      c_code           r_code      rr_code
  // ... can be DUMMY_CODEs in the sequence
  // (c_code, r_code) is being replaced by new_code:
  // l_pos  ...  target_pos   ...  r_pos  ...  rr_pos
  // l_code      new_code          DUMMY  ...  rr_code

  uint16_t nx_pos = seq[target_pos].next; // next occurrence of (c_code, r_code)
  if (nx_pos == r_pos) {
    // target_pos  ...  nx_pos  ...  rr_pos
    // c_code           c_code       c_code
    nx_pos = seq[nx_pos].next; // avoid overlapping pairs (nx_pos >= rr_pos)
  }


  if (l_pos != DUMMY_POS) {
    // replace (l_code, c_code) with (l_code, new_code)
    CODE l_code = seq[l_pos].code;
    assert(seq[l_pos].code != DUMMY_CODE);
    removeLink_SQ(rds, l_pos); // does not update seq[l_pos].prev and seq[l_pos].next
    if ((l_pair = locatePair(rds, l_code, c_code)) != NULL) {
      if (l_pair->f_pos == l_pos) {
        // l_pair was first occurrence of (l_code, c_code), after removal it is seq[l_pos].next
	      l_pair->f_pos = seq[l_pos].next;
      }
      decrementPair(rds, l_pair);
    }
    if ((c_pair = locatePair(rds, l_code, new_code)) == NULL) {
      // have not seen (l_code, new_code) before -> it becomes first occurrence
      seq[l_pos].prev = DUMMY_POS;
      seq[l_pos].next = DUMMY_POS;
      createPair(rds, l_code, new_code, l_pos);
    } else {
      // seq[l_pos] becomes new last occurrence of (l_code, new_code)
      seq[l_pos].prev = c_pair->b_pos;
      seq[l_pos].next = DUMMY_POS;
      seq[c_pair->b_pos].next = l_pos;
      c_pair->b_pos = l_pos;
      incrementPair(rds, c_pair);
    }
  }

  // replace (c_code, r_code) with (new_code, DUMMY_CODE) in sequence
  removeLink_SQ(rds, target_pos); // does not update seq[target_pos].prev and seq[target_pos].next
  removeLink_SQ(rds, r_pos); // does not update seq[r_pos].prev and seq[r_pos].next
  seq[target_pos].code = new_code;
  seq[r_pos].code = DUMMY_CODE;
  
  if (rr_pos != DUMMY_POS) {
    // replace (r_code, rr_code) with (new_code, rr_code)
    CODE rr_code = seq[rr_pos].code;
    assert(rr_code != DUMMY_CODE);
    if ((r_pair = locatePair(rds, r_code, rr_code)) != NULL) {
      if (r_pair->f_pos == r_pos) {
        // r_pair was first occurrence of (r_code, rr_code), after removal it is seq[r_pos].next
	      r_pair->f_pos = seq[r_pos].next;
      }
      decrementPair(rds, r_pair);
    }

    if (target_pos+1 == rr_pos-1) {
      // target_pos  r_pos  rr_pos (no DUMMY_CODEs in between)
      // target_pos+1 == r_pos (DUMMY_CODE)
      seq[target_pos+1].prev = rr_pos; // point to next valid position on the right
      seq[target_pos+1].next = target_pos; // point to next valid position on the left
    } else {
      // target_pos target_pos+1  ...  r_pos  ...  rr_pos-1 rr_pos (DUMMY_CODEs in between)
      seq[target_pos+1].prev = rr_pos; // point to next valid position on the right
      seq[target_pos+1].next = DUMMY_POS; 
      seq[rr_pos-1].prev = DUMMY_POS;
      seq[rr_pos-1].next = target_pos; // point to next valid position on the left
    }
    
    if (nx_pos > rr_pos) {
      if ((c_pair = locatePair(rds, new_code, rr_code)) == NULL) {
        // have not seen (new_code, rr_code) before -> it becomes first occurrence
        seq[target_pos].prev = DUMMY_POS;
        seq[target_pos].next = DUMMY_POS;
        createPair(rds, new_code, rr_code, target_pos);
      } else {
        // seq[target_pos] becomes new last occurrence of (new_code, rr_code)
        seq[target_pos].prev = c_pair->b_pos;
        seq[target_pos].next = DUMMY_POS;
        seq[c_pair->b_pos].next = target_pos;
        c_pair->b_pos = target_pos;
        incrementPair(rds, c_pair);
      }
    } else {
      // if (nx_pos != rr_pos) {
      //   std::cout << c_code << " " << r_code << " -> " << new_code << std::endl;
      //   std::cout << l_pos << " " << target_pos << " " << r_pos << " " << rr_pos << " " << nx_pos << " " << rightPos_SQ(rds, nx_pos) << std::endl;
      //   for (uint16_t i = l_pos; i < rightPos_SQ(rds, nx_pos); i++) {
      //     std::cout << i << ": " << seq[i].code << std::endl;
      //   }
      // }
      assert(nx_pos == rr_pos);
      // target_pos  ...  r_pos  ...  rr_pos=nx_pos
      // new_code         DUMMY       rr_code (-> newcode in next update)
      seq[target_pos].next = seq[target_pos].prev = DUMMY_POS;
    }

  } else if (target_pos < rds->txt_len - 1) {
    // assert(false);
    // rr_pos == DUMMY_POS
    // end of sequence edge case
    // target_pos target_pos+1  ...  r_pos  ...  rr_pos
    // newcode    DUMMY         ...  DUMMY  ...  DUMMY
    assert(seq[target_pos+1].code == DUMMY_CODE);
    seq[target_pos+1].prev = DUMMY_POS;
    seq[target_pos+1].next = target_pos; // point to next valid position on the left
    seq[r_pos].prev = seq[r_pos].next = DUMMY_POS;
  }
}

// priority queue

// insert pair into priority queue at frequency p_num
void insertPair_PQ(RDS *rds, PAIR *target, uint64_t p_num) {
  PAIR *tmp;

  if (p_num >= rds->p_max) {
    p_num = 0;
  }

  // insert at head of doubly-linked queue at p_que[p_num]
  tmp = rds->p_que[p_num];
  rds->p_que[p_num] = target;
  target->p_prev = NULL;
  target->p_next = tmp;
  if (tmp != NULL) {
    tmp->p_prev = target;
  }
}

void removePair_PQ(RDS *rds, PAIR *target, uint64_t p_num) {
  if (p_num >= rds->p_max) {
    p_num = 0;
  }
  
  // remove target from doubly-linked queue at p_que[p_num]
  if (target->p_prev == NULL) {
    // target is at head of queue
    rds->p_que[p_num] = target->p_next;
    if (target->p_next != NULL) {
      target->p_next->p_prev = NULL;
    }
  } else {
    // target is in middle or end of queue
    target->p_prev->p_next = target->p_next;
    if (target->p_next != NULL) {
      target->p_next->p_prev = target->p_prev;
    }
  }
}


void incrementPair(RDS *rds, PAIR *target) {
  if (target->freq >= rds->p_max) {
    // target stays in the same bucket
    target->freq++;
    return;
  }
  removePair_PQ(rds, target, target->freq);
  target->freq++;
  insertPair_PQ(rds, target, target->freq);
}

void decrementPair(RDS *rds, PAIR *target) {
  if (target->freq > rds->p_max) {
    target->freq--;
    return;
  }
  
  if (target->freq == 1) {
    // remove pair completely as frequency becomes zero
    destructPair(rds, target);
  } else {
    removePair_PQ(rds, target, target->freq);
    target->freq--;
    insertPair_PQ(rds, target, target->freq);
  }
}

PAIR* getMaxPair(RDS *rds) {
  PAIR **p_que = rds->p_que;
  PAIR *p, *max_pair;
  uint64_t max;

  if (p_que[0] != NULL) {
    p = p_que[0];
    max = 0; max_pair = NULL;
    while (p != NULL) {
      if (max < p->freq) {
        max = p->freq;
        max_pair = p;
      }
      p = p->p_next;
    }
  } else {
    // do not return freq=1 pairs
    max_pair = NULL;
    for (uint64_t i = rds->p_max-1; i > 1; i--) {
      if (p_que[i] != NULL) {
        max_pair = p_que[i];
        break;
      }
    }
  }
  return max_pair;
}

// hash table
#define INIT_HASH_NUM 15
static const uint64_t primes[] = {
  /* 0*/  8 + 3,
  /* 1*/  16 + 3,
  /* 2*/  32 + 5,
  /* 3*/  64 + 3,
  /* 4*/  128 + 3,
  /* 5*/  256 + 27,
  /* 6*/  512 + 9,
  /* 7*/  1024 + 9,
  /* 8*/  2048 + 5,
  /* 9*/  4096 + 3,
  /*10*/  8192 + 27,
  /*11*/  16384 + 43,
  /*12*/  32768 + 3,
  /*13*/  65536 + 45,
  /*14*/  131072 + 29,
  /*15*/  262144 + 3,
  /*16*/  524288 + 21,
  /*17*/  1048576 + 7,
  /*18*/  2097152 + 17,
  /*19*/  4194304 + 15,
  /*20*/  8388608 + 9,
  /*21*/  16777216 + 43,
  /*22*/  33554432 + 35,
  /*23*/  67108864 + 15,
  /*24*/  134217728 + 29,
  /*25*/  268435456 + 3,
  /*26*/  536870912 + 11,
  /*27*/  1073741824 + 85,
	  0
};
#define hash_val(P, A, B) (((A)*(B))%primes[P])

PAIR *locatePair(RDS *rds, CODE left, CODE right) {
  uint64_t h = hash_val(rds->h_num, left, right);
  PAIR *p = rds->h_first[h];

  while (p != NULL) {
    if (p->left == left && p->right == right) {
      return  p;
    }
    p = p->h_next;
  }
  return NULL;
}

void growHashtable(RDS *rds) {
  rds->h_num++; // doubles size
  rds->h_first = (PAIR**)realloc(rds->h_first, sizeof(PAIR*) * primes[rds->h_num]);

  // empty existing hash table
  for (uint64_t i = 0; i < primes[rds->h_num]; i++) {
    rds->h_first[i] = NULL;
  }
  // rehash all pairs from priority queue
  // TODO: replace with for (uint64_t i = 0; i < rds->p_max; i++) ?
  for (uint64_t i = 1; ; i++) {
    if (i == rds->p_max) i = 0;
    Pair* p = rds->p_que[i];
    while (p != NULL) {
      p->h_next = NULL;
      uint64_t h = hash_val(rds->h_num, p->left, p->right);
      Pair* q = rds->h_first[h];
      rds->h_first[h] = p;
      p->h_next = q;
      p = p->p_next;
    }
    if (i == 0) break;
  }
}

PAIR *createPair(RDS *rds, CODE left, CODE right, uint16_t f_pos) {
  PAIR *pair = (PAIR*)malloc(sizeof(PAIR));

  pair->left  = left;
  pair->right = right;
  pair->freq = 1;
  pair->f_pos = pair->b_pos = f_pos;
  pair->p_prev = pair->p_next = NULL;

  rds->num_pairs++;

  if (rds->num_pairs >= primes[rds->h_num]) {
    growHashtable(rds);
  }

  uint64_t h = hash_val(rds->h_num, left, right);
  PAIR* q = rds->h_first[h];
  rds->h_first[h] = pair;
  pair->h_next = q;
  
  insertPair_PQ(rds, pair, 1);

  return pair;
}


void destructPair(RDS *rds, PAIR *target) {
  uint64_t h = hash_val(rds->h_num, target->left, target->right);
  PAIR *p = rds->h_first[h];
  PAIR *q = NULL;

  removePair_PQ(rds, target, target->freq);

  // search for pair in hash table
  while (p != NULL) {
    if (p->left == target->left && p->right == target->right) {
      break;
    }
    q = p;
    p = p->h_next;
  }
  assert(p != NULL); // have found pair to be deleted

  if (q == NULL) {
    rds->h_first[h] = p->h_next; // pair was first in chain
  } else {
    q->h_next = p->h_next; // pair was in middle or end of chain
  }
  free(target);
  rds->num_pairs--;
}

// destroy all entries in the priority queue for a given frequency p_num
void resetPQ(RDS *rds, uint64_t p_num) {
  PAIR **p_que = rds->p_que;
  PAIR *pair = p_que[p_num];
  PAIR *q;
  p_que[p_num] = NULL;
  while (pair != NULL) {
    q = pair->p_next;
    destructPair(rds, pair);
    pair = q;
  }
}

void initRDS_by_counting_pairs(RDS *rds) {
  SEQ *seq = rds->seq;
  CODE A, B;
  PAIR *pair;

  // does not account for overlapping pairs
  for (uint64_t i = 0; i < rds->txt_len - 1; i++) {
    A = seq[i].code;
    B = seq[i+1].code;
    if ((pair = locatePair(rds, A, B)) == NULL) {
      pair = createPair(rds, A, B, i); // inits freq to 1
    } else {
      // seq[i] becomes new last occurrence of (A,B)
      seq[i].prev = pair->b_pos;
      seq[i].next = DUMMY_POS;
      seq[pair->b_pos].next = i; // link previous last occurrence to new last occurrence
      pair->b_pos = i; // update last occurrence index
      incrementPair(rds, pair);
    }
  }
  // remove all pairs with frequency 1 from priority queue
  resetPQ(rds, 1);
}

void destructRDS(RDS *rds) {
  free(rds->seq);
  free(rds->h_first);
  free(rds->p_que);
  free(rds);
}


uint64_t replacePairs(RDS *rds, PAIR *max_pair, CODE new_code) {
  uint16_t i, j;
  uint64_t num_replaced = 0;
  SEQ *seq = rds->seq;

  i = max_pair->f_pos;
  while (i != DUMMY_POS) {
    j = seq[i].next;
    // account for overlapping occurrences
    if (j == rightPos_SQ(rds, i)) {
      j = seq[j].next;
    }
    updateBlock_SQ(rds, new_code, i);
    i = j;
    num_replaced++;
  }
  // we count overlapping pairs, so after replacement freq does not have to be 1
  if (max_pair->freq != 1) { 
    destructPair(rds, max_pair); 
  }
  resetPQ(rds, 1);
  return num_replaced;
}

#define INIT_DICTIONARY_SIZE (256*1024)
#define DICTIONARY_SCALING_FACTOR (1.25)

DICT *createDict(uint64_t txt_len, uint16_t CHAR_SIZE) {
  uint i;
  DICT* dict = (DICT*)malloc(sizeof(DICT));
  dict->txt_len = txt_len;
  dict->buff_size = INIT_DICTIONARY_SIZE;
  dict->rule = (RULE*)malloc(sizeof(RULE)*dict->buff_size);
  dict->seq_len = 0;
  dict->comp_seq = NULL;
  dict->num_rules = 0;

  for (i = 0; i < dict->buff_size; i++) {
    dict->rule[i].left = DUMMY_CODE;
    dict->rule[i].right = DUMMY_CODE;
  }

  for (i = 0; i < CHAR_SIZE+1; i++) {
    dict->rule[i].left  = (CODE)i;
    dict->rule[i].right = DUMMY_CODE;
    dict->num_rules++;
  }

  return dict;
}

CODE addNewPair(DICT *dict, PAIR *max_pair) {
  RULE *rule = dict->rule;
  CODE new_code = dict->num_rules++;

  rule[new_code].left = max_pair->left;
  rule[new_code].right = max_pair->right;

  if (dict->num_rules >= dict->buff_size) {
    dict->buff_size *= DICTIONARY_SCALING_FACTOR;
    dict->rule = (RULE*)realloc(dict->rule, sizeof(RULE)*dict->buff_size);
    if (dict->rule == NULL) {
      puts("Memory reallocate error (rule) at addDict.");
      exit(1);
    }
  }
  return new_code;
}

typedef struct CompSeq {
  CODE* seq;
  uint64_t len;
} COMPSEQ;

CompSeq getCompSeq(RDS *rds) {
  SEQ *seq = rds->seq;

  uint64_t i = 0; uint64_t seq_len = 0;
  while (i < rds->txt_len) {
    if (seq[i].code == DUMMY_CODE) {
      i = seq[i].prev;
      continue;
    }
    seq_len++;
    i++;
  }

  CODE* comp_seq = (CODE*)malloc(sizeof(CODE)*seq_len);
  i = 0;
  uint16_t j = 0;
  while (i < rds->txt_len) {
    if (seq[i].code == DUMMY_CODE) {
      i = seq[i].prev;
      continue;
    }
    comp_seq[j++] = seq[i].code;
    i++;
  }
  COMPSEQ res = {comp_seq, seq_len};
  return res;
}

EDICT *convertDict(DICT *dict, uint16_t CHAR_SIZE) {
  EDICT *edict = (EDICT*)malloc(sizeof(EDICT));
  edict->txt_len = dict->txt_len;
  edict->seq_len = dict->seq_len;
  edict->num_rules = dict->num_rules;
  edict->comp_seq = dict->comp_seq;
  edict->rule  = dict->rule;
  edict->tcode = (CODE*)malloc(sizeof(CODE)*dict->num_rules);

  for (uint64_t i = 0; i <= CHAR_SIZE; i++) {
    edict->tcode[i] = i;
  }
  for (uint64_t i = CHAR_SIZE+1; i < dict->num_rules; i++) {
    edict->tcode[i] = DUMMY_CODE;
  }

  free(dict);
  return edict;
}

struct Node {
    uint64_t freq;
    CODE symbol;      // >=0 for leaves, -1 for internal
    Node* left;
    Node* right;

    Node(uint32_t f, int s, Node* L=nullptr, Node* R=nullptr)
        : freq(f), symbol(s), left(L), right(R) {}
    ~Node() {
        delete left;
        delete right;
    }
};
struct NodeCmp {
    bool operator()(const Node* a, const Node* b) const {
        return a->freq > b->freq;
    }
};
Node* buildTree(uint64_t* freq, uint16_t size) {
    std::priority_queue<Node*, std::vector<Node*>, NodeCmp> pq;

    for (int i=0; i < size; i++)
        if (freq[i] > 0)
            pq.push(new Node(freq[i], i));

    if (pq.empty())
        return new Node(1, 0); // dummy tree if input empty

    while (pq.size() > 1) {
        Node* a = pq.top(); pq.pop();
        Node* b = pq.top(); pq.pop();
        pq.push(new Node(a->freq + b->freq, DUMMY_CODE, a, b));
    }
    return pq.top();
}
void extractLengths(Node* n, int depth, uint64_t* lengths) {
    if (!n) return;
    if (n->symbol != DUMMY_CODE) {
        lengths[n->symbol] = depth;
        return;
    }
    extractLengths(n->left,  depth + 1, lengths);
    extractLengths(n->right, depth + 1, lengths);
}

void RunRepair(uint64_t batchsize, char* filename) {

    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return;
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
    uint16_t CHAR_SIZE = 1000 - min_val + 1;
    std::cout << "Read " << count << " entries from " << filename << " -> min_val=" << min_val << " and CHAR_SIZE=" << CHAR_SIZE << std::endl;

    // global huff
    uint64_t* freq = (uint64_t*) calloc(UINT16_MAX, sizeof(uint64_t));
    uint64_t* lengths = (uint64_t*) calloc(UINT16_MAX, sizeof(uint64_t));

    uint64_t final_size = 0;
    double avg_num_rules = 0.0;
    double avg_seq_len = 0.0;
    for (uint64_t start = 0; start < count; start += batchsize) {
      
      uint64_t size_w = std::min(batchsize, count - start);

      // init sequence
      SEQ* seq = (SEQ*) malloc(size_w * sizeof(SEQ));
      for (uint16_t i = 0; i < size_w; i++) {
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
      uint64_t p_max = (uint64_t) ceil(sqrt((double) size_w)) + 10;
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


      uint64_t num_rules = 0;
      uint64_t num_replaced = 0;
      PAIR* max_pair;
      CODE new_code = 0;
      while ((max_pair = getMaxPair(rds)) != NULL) {
        num_rules++;
        new_code = addNewPair(dict, max_pair);
        num_replaced += replacePairs(rds, max_pair, new_code);
      }
      CODE max_code = new_code;
      // std::cout << "Total number of replacements: " << num_replaced << std::endl;
      // std::cout << "Total number of rules: " << num_rules << std::endl;
      CompSeq comp_seq = getCompSeq(rds);
      // std::cout << "Compressed sequence length: " << comp_seq.len << std::endl;

      avg_num_rules += (double) num_rules / (count/batchsize + 1);
      avg_seq_len += (double) comp_seq.len / (count/batchsize + 1);

      // PAIR* max_pair = getMaxPair(rds);
      // replacePairs(rds, max_pair, 1001);

      // std::cout << "Number of distinct pairs: " << rds->num_pairs << std::endl;
      // for (uint64_t i = 0; i < p_max; i++) {
      //     PAIR* p = rds->p_que[i];
      //     if (p != NULL) {
      //         std::cout << "Frequency " << i << ": ";
      //         while (p != NULL) {
      //             std::cout << "(" << p->left << "," << p->right << ", freq=" << p->freq << ") ";
      //             p = p->p_next;
      //         }
      //         std::cout << std::endl;
      //     }
      // }
      EDICT* edict = convertDict(dict, CHAR_SIZE);
      uint64_t cfg_bits = EncodeCFG(edict, CHAR_SIZE);
      DestructEDict(edict);
      destructRDS(rds);

      final_size += (cfg_bits / 8 + 1);
      // final_size += 2*comp_seq.len; 

      // global huff
      for (uint64_t i = 0; i < comp_seq.len; i++) freq[comp_seq.seq[i]]++;

      // huff
      // uint64_t* freq = (uint64_t*) calloc(max_code+1, sizeof(uint64_t));
      // uint64_t* lengths = (uint64_t*) calloc(max_code+1, sizeof(uint64_t));
      // for (uint64_t i = 0; i < comp_seq.len; i++) freq[comp_seq.seq[i]]++;
      // Node* tree = buildTree(freq, max_code + 1);
      // extractLengths(tree, 0, lengths);

      // uint64_t huffcoded_seq_bits = 0;
      // uint64_t nonzero_symbols = 0;
      // for (CODE code = 0; code <= max_code; code++) {
      //   huffcoded_seq_bits += freq[code] * lengths[code];
      //   nonzero_symbols += (freq[code] > 0);
      // }
      // final_size += (huffcoded_seq_bits / 8) + 1;
      // final_size += nonzero_symbols; // optimal 1 byte length canonical

      // free(freq);
      // free(lengths);
      // delete tree;
    }

    // global huff
    Node* tree = buildTree(freq, UINT16_MAX);
    extractLengths(tree, 0, lengths);

    uint64_t huffcoded_seq_bits = 0;
    uint64_t nonzero_symbols = 0;
    for (CODE code = 0; code < UINT16_MAX; code++) {
      huffcoded_seq_bits += freq[code] * lengths[code];
      nonzero_symbols += (freq[code] > 0);
    }
    final_size += (huffcoded_seq_bits / 8) + 1;
    final_size += nonzero_symbols * 4;

    free(freq);
    free(lengths);
    delete tree;


    std::cout << "final size: " << final_size << " " << (double) final_size / (count*2.0) << std::endl;
    std::cout << "avg_num_rules: " << avg_num_rules << ", avg_seq_len: " << avg_seq_len << std::endl;
    free(TB);
}


int main(int argc, char *argv[]) {
  assert(argc == 2);
  char* filename = argv[1];
  // "KQQKBN.egtb";
  // 4.95 GB -> 522 MB with zip
  // 441 MB without huff-encoded seq
  // 280 MB with huff-encoding but without trees
  // 344 MB with huff-encoding with trees
  // 295 MB global huff
  
  // "KBNKQQ.egtb";
  // 5.49 GB -> 1.19 GB with zip
  // 1.25 GB without huff-encoded seq
  // 838 MB with huff-encoding but without trees
  // 971 MB with huff-encoding but with trees
  // 865 MB global huff

  // target < 1.41 GB



  // KQBKNP.egtb
  // 32.47 GB -> 3.97 GB with zip

  // KNPKQB.egtb
  // -> 

  // target < 8GB

  // RunRepair(UINT16_MAX, filename);
  RunRepair(8192, filename);
  return 0;
}