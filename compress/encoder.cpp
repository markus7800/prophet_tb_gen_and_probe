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

#include "encoder.h"

#define OP 1
#define CP 0

void writeBits(BITOUT *b, uint16_t x, uint64_t wblen) {
    b->bit_count += wblen;
}
BITOUT *createBitout() {
    BITOUT *b = (BITOUT*)malloc(sizeof(BITOUT));
    b->bit_count = 0;
    return b;
}
void flushBitout(BITOUT *b) {

}

// ceil(log2(n))
uint64_t bits (uint16_t n) {
  uint64_t b = 0;
  while (n)
    { b++; n >>= 1; }
  return b;
}

void putLeaf(CODE numcode, CODE lvcode, BITOUT *bitout) {
  uint64_t bitslen = bits(numcode);
  writeBits(bitout, lvcode, bitslen);
}

void putParen(uint16_t b, BITOUT *bitout) {
  if (b == OP) {
    writeBits(bitout, OP, 1);
  }
  else {
    writeBits(bitout, CP, 1);
  }
}

void encodeCFG_rec(CODE code, EDICT *dict, BITOUT *bitout, uint16_t& newcode, uint16_t CHAR_SIZE) {
//   static uint newcode = CHAR_SIZE;

  if (dict->tcode[code] == DUMMY_CODE) {
    encodeCFG_rec(dict->rule[code].left, dict, bitout, newcode, CHAR_SIZE);
    encodeCFG_rec(dict->rule[code].right, dict, bitout, newcode, CHAR_SIZE);
    dict->tcode[code] = ++newcode;
    putParen(CP, bitout);
  }
  else {
    putParen(OP, bitout);
    if (code < CHAR_SIZE) {
      putLeaf(newcode, code, bitout);
    }
    else {
      putLeaf(newcode, dict->tcode[code], bitout);
    }
  }
}

uint64_t EncodeCFG(EDICT *dict, uint16_t CHAR_SIZE) {
  BITOUT *bitout;
  bitout = createBitout();
  for (uint64_t i = 0; i < dict->seq_len; i++) {
    encodeCFG_rec(dict->comp_seq[i], dict, bitout, CHAR_SIZE, CHAR_SIZE);
    putParen(CP, bitout);
  }
  flushBitout(bitout);
  return bitout->bit_count;
}


void DestructEDict(EDICT *dict) {
  free(dict->rule);
  free(dict->comp_seq);
  free(dict->tcode);
  free(dict);
}

