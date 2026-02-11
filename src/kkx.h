
#ifndef KKX_H_INCLUDED
#define KKX_H_INCLUDED

#include <cstdint>
#include <iostream>
#include "types.h"
#include "bitboard.h"
#include "uci.h"

namespace Prophet {

void enumerate_kkx();

#define N_KKX 462
extern int8_t KKX_KNTM_SQ[N_KKX];
extern int8_t KKX_KTM_SQ[N_KKX];

// { 0, 1, 2, 3, 9, 10, 11, 18, 19, 27} -> { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9}
inline int8_t kkx_kntm_sq_to_ix(Square kntm_sq) {
    int8_t x = kntm_sq;
    return x - (((9 + (x>>3)) * (x >> 3)) >> 1);
}

extern int16_t KKX_IX_TABLE[10][64];
void init_kkx_table();

// squares have to be already transformed to canonical
inline int16_t get_kkx_ix(Square kntm, Square ktm) {
    int16_t kkx_ix = KKX_IX_TABLE[kkx_kntm_sq_to_ix(kntm)][ktm];
    if (kkx_ix == -1) {
        printf("Tried to access KKX_IX_TABLE[%d][%d] = -1\n", kntm, ktm);
        assert(false);
    } else {
        return kkx_ix;
    }
}

void enumerate_kkp();

#define N_KKP 1806
extern int8_t KKP_KNTM_SQ[N_KKP];
extern int8_t KKP_KTM_SQ[N_KKP];

// { 0, 1, 2, 3, 4, 8, 9, 10, 11, 16, 17, ..., 59} -> {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, ..., 31}
inline int kkp_kntm_sq_to_ix(Square kntm_sq) {
    int8_t x = kntm_sq;
    return x - (x >> 3) * 4;
}

extern int16_t KKP_IX_TABLE[32][64];
void init_kkp_table();

// squares have to be already transformed to canonical
inline int16_t get_kkp_ix(Square kntm, Square ktm) {
    int16_t kkp_ix = KKP_IX_TABLE[kkp_kntm_sq_to_ix(kntm)][ktm];
    if (kkp_ix == -1) {
        printf("Tried to access KKP_IX_TABLE[%d][%d] = -1\n", kntm, ktm);
        assert(false);
    } else {
        return kkp_ix;
    }
}


#define N_EP 14
extern const Square EP_PAWN[N_EP];
extern const Square EP_CAP_PAWN[N_EP]; 
extern const uint64_t EP_IX[8][3];

} // namespace Prophet

#endif
