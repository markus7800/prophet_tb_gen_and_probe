#ifndef ENCODERINCLUDED
#define ENCODERINCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "repair.h"

typedef struct EncodeDictionary {
  uint64_t txt_len;
  uint64_t seq_len;
  uint64_t num_rules;
  CODE *comp_seq;
  RULE *rule;
  CODE *tcode;
} EDICT;

// mock
typedef struct bit_output {
    uint64_t bit_count;
} BITOUT;
void writeBits(BITOUT *b, uint16_t x, uint64_t wblen);
BITOUT *createBitout();
void flushBitout(BITOUT *b);

uint64_t EncodeCFG(EDICT *dict, uint16_t CHAR_SIZE);
void encodeCFG_rec(CODE code, EDICT *dict, BITOUT *bitout, uint64_t& newcode, uint16_t CHAR_SIZE);
void putLeaf(uint64_t numcode, CODE lcode, BITOUT *bitout);
void putParen(uint16_t b, BITOUT *bitout);
void DestructEDict(EDICT *dict);
#endif
