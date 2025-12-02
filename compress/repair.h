
#ifndef REPAIR_H
#define REPAIR_H

#include <stdio.h>
#include <stdlib.h>
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

#endif