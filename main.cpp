#include <iostream>
#include "perft.h"
#include "bitboard.h"
#include "position.h"
#include "kkx.h"
#include "kkx_index.h"

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
    // check_transforms();
    // Stockfish::enumerate_kkp();

    init_kkx_index();

    std::vector<PieceType> stm_pieces = {};
    std::vector<PieceType> sntm_pieces = {QUEEN, ROOK, BISHOP, KNIGHT};
    KKXIndex index = KKXIndex(stm_pieces, sntm_pieces);
    std::cout << index.num_positions() << std::endl;

    uint64_t count = 0;
    uint64_t matches = 0;
    EGPosition pos;

    TimePoint t0 = now();
    for (uint64_t ix = 0; ix < index.num_positions(); ix++) {

        std::memset(&pos, 0, sizeof(EGPosition));
        index.pos_at_ix(pos, ix);
        if (pos.is_legal_checkmate()) {
            count++;
            // std::cout << pos << std::endl;
        }
        // std::cout << pos << std::endl;
        // count++;

        uint64_t ix2 = index.ix_from_pos(pos);
        matches += (ix == ix2);
        if (ix != ix2) {
            std::cout << ix << " vs " << ix2 << std::endl;
            break;
        }
        
        // if (count == 5) { break; }
    }

    std::cout << "Matches count: " << matches << std::endl;
    std::cout << "Checkmate count: " << count << std::endl;

    TimePoint t1 = now();
    std::cout << "Finished in " << (t1-t0) / 1000.0 << std::endl;

    return 0;
}