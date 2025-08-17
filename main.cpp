#include <iostream>
#include "perft.h"
#include "bitboard.h"
#include "position.h"
#include "kkx.h"

namespace Stockfish {


void enumerate_kkp() {
    int count = 0;
    for (Square p_sq = SQ_A1; p_sq <= SQ_H8; ++p_sq) {
        if (rank_of(p_sq) < RANK_2 || rank_of(p_sq) > RANK_7 || file_of(p_sq) > FILE_D) {
            continue;
        }
        for (Square ktm_sq = SQ_A1; ktm_sq <= SQ_H8; ++ktm_sq) {
            if (ktm_sq == p_sq) { continue; }
            for (Square kntm_sq = SQ_A1; kntm_sq <= SQ_H8; ++kntm_sq) {
                if (kntm_sq == p_sq || kntm_sq == ktm_sq) { continue; }
                if ((PseudoAttacks[KING][ktm_sq] & kntm_sq) == 0) {
                    count++;
                }
            }
        }
    }
    std::cout << "KKP Count: " << count << std::endl;
    
}

void check_transforms() {

    int kkx_index[64][64][3];
    int count = 0;

    for (Square ktm_sq = SQ_A1; ktm_sq <= SQ_H8; ++ktm_sq) {
        int8_t horizontal_flip = file_of(ktm_sq) > FILE_D ? 7 : 0;
        int8_t vertical_flip = rank_of(ktm_sq) > RANK_4 ? 56 : 0;
        int8_t flip = horizontal_flip ^ vertical_flip;

        int8_t new_ktm_sq = int8_t(ktm_sq) ^ flip;

        if (file_of((Square) new_ktm_sq) > FILE_D) {
            std::cout << "Square not in correct file " << square_to_uci(ktm_sq)<< " - " << square_to_uci((Square) new_ktm_sq) << std::endl;
        }
        if (rank_of((Square) new_ktm_sq) > RANK_4) {
            std::cout << "Square not in correct rank " << square_to_uci(ktm_sq)<< " - " << square_to_uci((Square) new_ktm_sq) << std::endl;
        }


        for (Square kntm_sq = SQ_A1; kntm_sq <= SQ_H8; ++kntm_sq) {
            int8_t new_kntm_sq = int8_t(kntm_sq) ^ flip;
            int8_t swap = 0;
            if (int8_t(rank_of((Square) new_ktm_sq)) > int8_t(file_of((Square) new_ktm_sq))) {
                swap = 3;
            }
            else if (int8_t(rank_of((Square) new_ktm_sq)) == int8_t(file_of((Square) new_ktm_sq))) {
                if (int8_t(rank_of((Square) new_kntm_sq)) > int8_t(file_of((Square) new_kntm_sq))) {
                    swap = 3;
                }
            }
            new_ktm_sq = ((new_ktm_sq >> swap) | (new_ktm_sq << swap)) & 63;
            new_kntm_sq = ((new_kntm_sq >> swap) | (new_kntm_sq << swap)) & 63;

            
            bool found = false;
            for (int i = 0; i < N_KKX; i++) {
                if (KKX_KTM_SQ[i] == new_ktm_sq && KKX_KNTM_SQ[i] == new_kntm_sq) {
                    kkx_index[ktm_sq][kntm_sq][0] = i;
                    found = true;
                }
            }
            if (!found) {
                kkx_index[ktm_sq][kntm_sq][0] = -1;
            } else {
                count++;
            }

            kkx_index[ktm_sq][kntm_sq][1] = flip;
            kkx_index[ktm_sq][kntm_sq][2] = swap;
        }
    }
    std::cout << "count: " << count << std::endl; // 3612


}

}


// Rotate 180 degrees
// sq' = sq ^ 63;

// Rotate 90 degrees Clockwise
// sq' = (((sq >> 3) | (sq << 3)) & 63) ^ 56;

// Rotate 90 degrees Anti-Clockwise
// sq' = (((sq >> 3) | (sq << 3)) & 63) ^ 7;

// Flip vertical
// sq' = sq ^ 56;

// Flip horizontal
// sq' = sq ^ 7;

// Flip diagonal a1-h8
// sq' = ((sq >> 3) | (sq << 3)) & 63;

// Flip diagonal a8-h1
// sq' = (((sq >> 3) | (sq << 3)) & 63) ^ 63;



int main() {
    Stockfish::Bitboards::init();
    Stockfish::Position::init();
    // Benchmark::perft("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 7, false);
    // enumerate_kk();
    check_transforms();
    // Stockfish::enumerate_kkp();
    return 0;
}