
#include <iostream>
#include <assert.h>

typedef int16_t CODE;
#define DUMMY_POS UINT16_MAX

typedef struct Sequence {
    CODE code;
    uint16_t next;
    uint16_t prev;
} SEQ;

typedef struct Pair {
    CODE left;
    CODE right;
    uint64_t freq;  // frequency of the pair in the sequence
    uint16_t f_pos; // first occurrence of pair in the sequence
    uint16_t b_pos; // last occurrence of pair in the sequence
    struct Pair *h_next; // next pair in hash table with same hash value
    struct Pair *p_next; // next pair in priority queue with same frequency
    struct Pair *p_prev; // previous pair in priority queue with same frequency
} PAIR;

typedef struct RePair_data_structures {
    uint64_t txt_len;
    SEQ *seq;

    // hash table for pairs
    uint64_t num_pairs;
    uint64_t h_num;
    PAIR **h_first;

    // priority queue for pair frequencies from 0 to p_max
    // p_que[i] points to the first pair with frequency i
    // all pairs with frequency i are linked via p_next and p_prev
    uint64_t p_max;
    PAIR **p_que; 
} RDS;

PAIR *locatePair(RDS *rds, CODE left, CODE right);
void growHashtable(RDS *rds);
void insertPair_PQ(RDS *rds, PAIR *target, uint64_t p_num);
void removePair_PQ(RDS *rds, PAIR *target, uint64_t p_num);
void incrementPair(RDS *rds, PAIR *target);
void decrementPair(RDS *rds, PAIR *target);
PAIR *createPair(RDS *rds, CODE left, CODE right, uint64_t f_pos);
void destructPair(RDS *rds, PAIR *target);
void resetPQ(RDS *rds, uint64_t p_num);
void initRDS_by_counting_pairs(RDS *rds);


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

PAIR *createPair(RDS *rds, CODE left, CODE right, uint64_t f_pos) {
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
      pair = createPair(rds, A, B, i);
    } else {
      seq[i].prev = pair->b_pos;
      seq[i].next = DUMMY_POS;
      seq[pair->b_pos].next = i;
      pair->b_pos = i;
      incrementPair(rds, pair);
    }
  }
  // remove all pairs with frequency 1 from priority queue
  resetPQ(rds, 1);
}


void RunRepair(int batchsize) {
    std::string filename = "KQQKBN.egtb";

    FILE *f = fopen(filename.c_str(), "rb");
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

    std::cout << "Read " << count << " entries from " << filename << std::endl;

    uint64_t size_w = batchsize;

    // init sequence
    SEQ* seq = (SEQ*) malloc(size_w * sizeof(SEQ));
    for (uint16_t i = 0; i < size_w; i++) {
        seq[i].code = TB[i];
        seq[i].next = DUMMY_POS;
        seq[i].prev = DUMMY_POS;
    }

    free(TB);

    // init hash table
    uint64_t h_num = INIT_HASH_NUM;
    Pair** h_first = (PAIR**) malloc(sizeof(PAIR*) * primes[h_num]);
    for (uint64_t i = 0; i < primes[h_num]; i++) {
        h_first[i] = NULL;
    }

    // init priority queue
    uint64_t p_max = batchsize; //(uint64_t) ceil(sqrt((double) size_w)) + 10;
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

    std::cout << "Number of distinct pairs: " << rds->num_pairs << std::endl;
    for (uint64_t i = 0; i < p_max; i++) {
        PAIR* p = rds->p_que[i];
        if (p != NULL) {
            std::cout << "Frequency " << i << ": ";
            while (p != NULL) {
                std::cout << "(" << p->left << "," << p->right << ") ";
                p = p->p_next;
            }
            std::cout << std::endl;
        }
    }
}


int main(int argc, char *argv[]) {
    RunRepair(UINT16_MAX);
    return 0;
}