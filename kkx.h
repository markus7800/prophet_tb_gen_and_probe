
#ifndef KKX_H_INCLUDED
#define KKX_H_INCLUDED

#include <cstdint>
#include <iostream>
#include "types.h"
#include "bitboard.h"
#include "uci.h"


const int8_t IX_TO_KNTM_SQ[10] = { 0, 1, 2, 3, 9, 10, 11, 18, 19, 27};

inline int8_t kntm_sq_to_ix(Square kntm_sq) {
    int8_t x = kntm_sq;
    return x - (((9 + (x>>3)) * (x >> 3)) >> 1);
}

int16_t KKX_IX_TABLE[64][64];

inline int8_t get_kkx_ktm_ix(Square ktm, Square kntm) {
    int8_t kkx_ix = KKX_IX_TABLE[ktm][kntm];
    if (kkx_ix == -1) {
        printf("Tried to access KKX_IX_TABLE[%d][%d] = -1\n", ktm, kntm);
        assert(false);
    } else {
        return kkx_ix;
    }
}

void init_kkx_table() {

    int count = 0;

    for (Square kntm_sq = SQ_A1; kntm_sq <= SQ_H8; ++kntm_sq) {
        int8_t horizontal_flip = file_of(kntm_sq) > FILE_D ? 7 : 0;
        int8_t vertical_flip = rank_of(kntm_sq) > RANK_4 ? 56 : 0;
        int8_t flip = horizontal_flip ^ vertical_flip;

        int8_t _new_kntm_sq = int8_t(kntm_sq) ^ flip;

        // check if king is in correct octant
        if (file_of((Square) _new_kntm_sq) > FILE_D) {
            std::cout << "Square not in correct file " << square_to_uci(kntm_sq)<< " - " << square_to_uci((Square) _new_kntm_sq) << std::endl;
        }
        if (rank_of((Square) _new_kntm_sq) > RANK_4) {
            std::cout << "Square not in correct rank " << square_to_uci(kntm_sq)<< " - " << square_to_uci((Square) _new_kntm_sq) << std::endl;
        }


        for (Square ktm_sq = SQ_A1; ktm_sq <= SQ_H8; ++ktm_sq) {
            Square new_kntm_sq = Square(int8_t(kntm_sq) ^ flip);
            Square new_ktm_sq = Square(int8_t(ktm_sq) ^ flip);
            int8_t swap = 0;
            if (int8_t(rank_of(new_kntm_sq)) > int8_t(file_of(new_kntm_sq))) {
                // sntm king is not in lower triangle a1 - d1 - d4
                swap = 3;
            }
            else if (int8_t(rank_of(new_kntm_sq)) == int8_t(file_of(new_kntm_sq))) {
                // sntm king is on diagonal a1 b2 c3 d4
                if (int8_t(rank_of(new_ktm_sq)) > int8_t(file_of(new_ktm_sq))) {
                    // for stm king to be on lower triangle a1 - h1 - h8
                    swap = 3;
                }
            }
            new_ktm_sq = Square((((int8_t)new_ktm_sq >> swap) | ((int8_t)new_ktm_sq << swap)) & 63);
            new_kntm_sq = Square((((int8_t)new_kntm_sq >> swap) | ((int8_t)new_kntm_sq << swap)) & 63);

            
            if (new_ktm_sq == new_kntm_sq || (unblockablechecks_bb(new_kntm_sq, KING) & square_bb(new_ktm_sq))) {
                KKX_IX_TABLE[ktm_sq][kntm_sq] = -1;
            } else {
                count++;

                Bitboard available_squares = 0;
                if (square_bb(new_kntm_sq) & DiagBB) {
                    available_squares = (DiagBB | BelowDiagBB) & (~unblockablechecks_bb(new_kntm_sq, KING)) & ~square_bb(new_kntm_sq);
                    assert(popcount(available_squares) ==  36 - 6 + 3 * (new_kntm_sq == SQ_A1));
                } else {
                    available_squares = ~(unblockablechecks_bb(new_kntm_sq,KING) | square_bb(new_kntm_sq));
                    assert(popcount(available_squares) == (64 - num_unblockablechecks(new_kntm_sq,KING) - 1));

                }
                assert (square_bb(new_ktm_sq) & available_squares);

                KKX_IX_TABLE[ktm_sq][kntm_sq] = (int) new_ktm_sq -  popcount((square_bb(new_ktm_sq) - 1) & ~available_squares);

                
                int8_t ktm_ix = get_kkx_ktm_ix(ktm_sq, kntm_sq);
                assert (new_ktm_sq == nth_set_sq(available_squares, ktm_ix));
            }


        }
    }
    std::cout << "init_kkx_table count: " << count << std::endl; // 3612
}


#endif