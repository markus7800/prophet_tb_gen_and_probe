
#ifndef KKX_H_INCLUDED
#define KKX_H_INCLUDED

#include <cstdint>
#include <iostream>
#include "types.h"
#include "bitboard.h"

using namespace Stockfish;

Square KSQs_NP_HALF_OCT[10] = {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1,
           SQ_B2, SQ_C2, SQ_D2,
                  SQ_C3, SQ_D3,
                         SQ_D4
};

Square KSQs_NP_OCT[16] = {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4
};

// 462 positions
void _enumerate_kk(int print_sqs) {
    if (print_sqs == 1) {
        std::cout << "int8_t KKX_KTM_SQ[N_KKX] = {" << std::endl;
    } else if (print_sqs == 2) {
        std::cout << "int8_t KKX_KNTM_SQ[N_KKX] = {" << std::endl;
    }
    int count = 0;
    for (Square ktm_sq: KSQs_NP_HALF_OCT) {
        for (Square kntm_sq = SQ_A1; kntm_sq <= SQ_H8; ++kntm_sq) {
            if (
                (int(rank_of(ktm_sq)) == int(file_of(ktm_sq))) &&
                (int(rank_of(kntm_sq)) > int(file_of(kntm_sq)))
            ) {
                continue;
            }
            if (ktm_sq != kntm_sq && (PseudoAttacks[KING][ktm_sq] & kntm_sq) == 0) {
                if (print_sqs == 1) {
                    printf("%2d,", int(ktm_sq));
                } else if (print_sqs == 2) {
                    printf("%2d,", int(kntm_sq));
                }
                count++;
            }
        }
        std::cout << std::endl;
    }
    if (print_sqs > 0) {
        std::cout << "};" << std::endl;
    } else {
        std::cout << "KK Count: " << count << std::endl;
    }
}
void enumerate_kk() {
    std::cout << "#define N_KKX 462" << std::endl;;
    _enumerate_kk(1);
    _enumerate_kk(2);
}


#define N_KKX 462
int8_t KKX_KTM_SQ[N_KKX] = {
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,
18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,
};
int8_t KKX_KNTM_SQ[N_KKX] = {
 2, 3, 4, 5, 6, 7,10,11,12,13,14,15,18,19,20,21,22,23,27,28,29,30,31,36,37,38,39,45,46,47,54,55,63,
 3, 4, 5, 6, 7,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
 0, 4, 5, 6, 7, 8,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
 0, 1, 5, 6, 7, 8, 9,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
 3, 4, 5, 6, 7,11,12,13,14,15,19,20,21,22,23,27,28,29,30,31,36,37,38,39,45,46,47,54,55,63,
 0, 4, 5, 6, 7, 8,12,13,14,15,16,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
 0, 1, 5, 6, 7, 8, 9,13,14,15,16,17,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
 0, 1, 2, 3, 4, 5, 6, 7,12,13,14,15,20,21,22,23,28,29,30,31,36,37,38,39,45,46,47,54,55,63,
 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,13,14,15,16,17,21,22,23,24,25,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
 0, 1, 2, 3, 4, 5, 6, 7, 9,10,11,12,13,14,15,21,22,23,29,30,31,37,38,39,45,46,47,54,55,63,
};

struct KKX_IX_T {
    int16_t ix;
    int8_t flip;
    int8_t swap;
};

KKX_IX_T KKX_IX_T_TABLE[64][64];

void init_kkx_index() {

    int count = 0;

    for (Square ktm_sq = SQ_A1; ktm_sq <= SQ_H8; ++ktm_sq) {
        int8_t horizontal_flip = file_of(ktm_sq) > FILE_D ? 7 : 0;
        int8_t vertical_flip = rank_of(ktm_sq) > RANK_4 ? 56 : 0;
        int8_t flip = horizontal_flip ^ vertical_flip;

        int8_t _new_ktm_sq = int8_t(ktm_sq) ^ flip;

        // check if king is in correct octant
        if (file_of((Square) _new_ktm_sq) > FILE_D) {
            std::cout << "Square not in correct file " << square_to_uci(ktm_sq)<< " - " << square_to_uci((Square) _new_ktm_sq) << std::endl;
        }
        if (rank_of((Square) _new_ktm_sq) > RANK_4) {
            std::cout << "Square not in correct rank " << square_to_uci(ktm_sq)<< " - " << square_to_uci((Square) _new_ktm_sq) << std::endl;
        }


        for (Square kntm_sq = SQ_A1; kntm_sq <= SQ_H8; ++kntm_sq) {
            int8_t new_ktm_sq = int8_t(ktm_sq) ^ flip;
            int8_t new_kntm_sq = int8_t(kntm_sq) ^ flip;
            int8_t swap = 0;
            // std::cout << int(ktm_sq) << " " << square_to_uci((Square) ktm_sq) << " flip:" << int(flip) << " - " << int(new_ktm_sq) << " " << square_to_uci((Square) new_ktm_sq) << " " << square_to_uci((Square) new_kntm_sq) << " r:" << int(rank_of((Square) new_ktm_sq)) << " f:" <<  int(file_of((Square) new_ktm_sq)) << std::endl;
            if (int8_t(rank_of((Square) new_ktm_sq)) > int8_t(file_of((Square) new_ktm_sq))) {
                // stm king is not in lower triangle a1 - d1 - d4
                swap = 3;
            }
            else if (int8_t(rank_of((Square) new_ktm_sq)) == int8_t(file_of((Square) new_ktm_sq))) {
                // stm king is on diagonal a1 b2 c3 d4
                if (int8_t(rank_of((Square) new_kntm_sq)) > int8_t(file_of((Square) new_kntm_sq))) {
                    // for sntm king to be on lower triangle a1 - h1 - h8
                    swap = 3;
                }
            }
            new_ktm_sq = ((new_ktm_sq >> swap) | (new_ktm_sq << swap)) & 63;
            new_kntm_sq = ((new_kntm_sq >> swap) | (new_kntm_sq << swap)) & 63;

            
            bool found = false;
            int16_t ix = -1;
            for (int i = 0; i < N_KKX; i++) {
                if (KKX_KTM_SQ[i] == new_ktm_sq && KKX_KNTM_SQ[i] == new_kntm_sq) {
                    ix = i;
                    found = true;
                }
            }
            if (found) {
                count++;
            }
            printf("KKX_IX_T_TABLE[%d][%d] = (%d,%d,%d)\n", ktm_sq, kntm_sq, ix, flip, swap);
            KKX_IX_T_TABLE[ktm_sq][kntm_sq] = {ix, flip, swap};

        }
    }
    std::cout << "init_kkx_index count: " << count << std::endl; // 3612
}

inline KKX_IX_T get_kkx_ix_t(Square ktm, Square kntm) {
    KKX_IX_T kkx_ix_tr = KKX_IX_T_TABLE[ktm][kntm];
    if (kkx_ix_tr.ix == -1) {
        printf("Tried to access KKX_IX_T_TABLE[%d][%d] = -1\n", ktm, kntm);
        exit(1);
    } else {
        return kkx_ix_tr;
    }
}

#endif