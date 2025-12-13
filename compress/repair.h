/* 
*  Copyright (c) 2011 Shirou Maruyama
* 
*   Redistribution and use in source and binary forms, with or without
*   modification, are permitted provided that the following conditions
*   are met:
* 
*   1. Redistributions of source code must retain the above Copyright
*      notice, this list of conditions and the following disclaimer.
*
*   2. Redistributions in binary form must reproduce the above Copyright
*      notice, this list of conditions and the following disclaimer in the
*      documentation and/or other materials provided with the distribution.
*
*   3. Neither the name of the authors nor the names of its contributors
*      may be used to endorse or promote products derived from this
*      software without specific prior written permission.
*/

#ifndef REPAIR_H
#define REPAIR_H

#include <stdio.h>
#include <stdlib.h>
#include <cstdint>
#include <string.h>
#include <math.h>

typedef uint16_t CODE;
#define DUMMY_POS UINT16_MAX
#define DUMMY_CODE INT16_MAX

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


typedef struct Rule {
  CODE left;
  CODE right;
} RULE;

typedef struct Dictionary {
  uint64_t txt_len;
  uint64_t num_rules;
  RULE *rule;
  uint64_t seq_len;
  CODE *comp_seq;
  uint64_t buff_size;
} DICT;

typedef struct EncodeDictionary {
  uint64_t txt_len;
  uint64_t seq_len;
  uint64_t num_rules;
  CODE *comp_seq;
  RULE *rule;
  CODE *tcode;
} EDICT;


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
uint64_t replacePairs(RDS *rds, PAIR *max_pair, CODE new_code);
PAIR* getMaxPair(RDS *rds);
PAIR* findPair(RDS *rds, CODE left, CODE right);
PAIR *createPair(RDS *rds, CODE left, CODE right, uint16_t f_pos);
void destructPair(RDS *rds, PAIR *target);
void resetPQ(RDS *rds, uint64_t p_num);
void initRDS_by_counting_pairs(RDS *rds);
void destructRDS(RDS *rds);
void getCompSeq(RDS *rds, DICT *dict);
CODE addNewPair(DICT *dict, PAIR *max_pair);
DICT *createDict(uint64_t txt_len, uint16_t CHAR_SIZE);
EDICT *convertDict(DICT *dict, uint16_t CHAR_SIZE);

#endif