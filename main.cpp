#include <iostream>
#include "perft.h"
#include "bitboard.h"
#include "position.h"
#include "kkx.h"
#include "kkx_index.h"
#include "eg_movegen.h"

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
void enumerate_kk_naive() {
    int count = 0;
    for (Square ktm_sq = SQ_A1; ktm_sq <= SQ_H8; ++ktm_sq) {
        for (Square kntm_sq = SQ_A1; kntm_sq <= SQ_H8; ++kntm_sq) {
            if (kntm_sq == ktm_sq) { continue; }
            if ((PseudoAttacks[KING][ktm_sq] & kntm_sq) == 0) {
                count++;
            }
        }
    }
    std::cout << "KK Naive Count: " << count << std::endl;
    
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

void test_kkx_index() {
    PieceType pts[2] = {QUEEN, ROOK};
    Color cs[2] = {WHITE, BLACK};

    Color stm = BLACK;

    std::vector<PieceType> stm_pieces = {ROOK};
    std::vector<PieceType> sntm_pieces = {QUEEN};
    KKXIndex index = KKXIndex(stm_pieces, sntm_pieces);

    EGPosition pos1;
    EGPosition pos2;
    for (Square p1_sq = SQ_A1; p1_sq <= SQ_H8; ++p1_sq) {
        for (Square p2_sq = SQ_A1; p2_sq <= SQ_H8; ++p2_sq) {
            if(p2_sq == p1_sq) { continue; }
            for (Square k1_sq = SQ_A1; k1_sq <= SQ_H8; ++k1_sq) {
                if (k1_sq == p1_sq || k1_sq == p2_sq) { continue; }
                for (Square k2_sq = SQ_A1; k2_sq <= SQ_H8; ++k2_sq) {
                    if (k2_sq == p1_sq || k2_sq == p2_sq || k2_sq == k1_sq) { continue; }
                    if ((PseudoAttacks[KING][k1_sq] & k2_sq) == 0) {
                        std::memset(&pos1, 0, sizeof(EGPosition));
                        std::memset(&pos2, 0, sizeof(EGPosition));
                        pos1.put_piece(make_piece(cs[0],pts[0]), p1_sq);
                        pos1.put_piece(make_piece(cs[1],pts[1]), p2_sq);
                        pos1.put_piece(B_KING, k1_sq);
                        pos1.put_piece(W_KING, k2_sq);
                        pos1.set_side_to_move(stm);

                        uint64_t ix = index.ix_from_pos(pos1);
                        index.pos_at_ix(pos2, ix);

                        if (!pos1.is_equal(pos2)) {
                            std::cout << pos1 << std::endl;
                            std::cout << "vs" << std::endl;
                            std::cout << pos2 << std::endl;
                            exit(1);
                        }
                    }
                }
            }
        }
    }
}

int main() {
    Stockfish::Bitboards::init();
    Stockfish::Position::init();
    // Benchmark::perft("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 7, false);
    // check_transforms();
    Stockfish::enumerate_kk_naive();

    init_kkx_index();

    // test_kkx_index();
    // return 0;

    std::vector<PieceType> stm_pieces = {};
    std::vector<PieceType> sntm_pieces = {QUEEN};
    KKXIndex index = KKXIndex(stm_pieces, sntm_pieces);
    std::cout << "Num positions: " << index.num_positions() << std::endl;

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
        if (pos.checkers(pos.side_to_move())) {
            EGMoveList moveList = EGMoveList<FORWARD>(pos);
            if (moveList.size() == 0) {
                // count++;
            }
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

    uint64_t NPOS = index.num_positions();
    int16_t* TB = (int16_t*) calloc(sizeof(int16_t), NPOS);

    // init
    int16_t LEVEL = 1;
    uint64_t N_LEVEL_POS = 0;
    for (uint64_t ix = 0; ix < NPOS; ix++) {
        std::memset(&pos, 0, sizeof(EGPosition));
        index.pos_at_ix(pos, ix);
        if (pos.is_legal_checkmate()) {
            TB[ix] = -LEVEL;
            N_LEVEL_POS++;
        }
    }
    std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;

    uint64_t iteration_counter = 0;
    while (N_LEVEL_POS > 0) {
        LEVEL++;
        N_LEVEL_POS = 0;
        for (uint64_t ix = 0; ix < NPOS; ix++) {
            // for all checkmate in LEVEL-1
            if (TB[ix] == -(LEVEL-1)) {
                std::memset(&pos, 0, sizeof(EGPosition));
                // std::cout << "POSITION:" << std::endl;
                index.pos_at_ix(pos, ix);
                // std::cout << pos << std::endl;
                for (Move move : EGMoveList<REVERSE>(pos)) {
                    pos.move_piece(move.from_sq(), move.to_sq()); // non-capture
                    pos.flip_side_to_move();
                    uint64_t win_ix = index.ix_from_pos(pos);
                    // std::cout << "1." << move_to_uci(move, false) << " " << win_ix << std::endl;
                    // make reverse move and mark position as win in LEVEL
                    // EGPosition pos2;

                    if (TB[win_ix] == 0) {
                        TB[win_ix] = LEVEL;
                        N_LEVEL_POS++;

                        // std::cout << pos << std::endl;
                        // std::cout << "Set level of " << win_ix << " to " << LEVEL << std::endl;
                        // std::memset(&pos2, 0, sizeof(EGPosition));
                        // index.pos_at_ix(pos2, win_ix);
                        // std::cout << "vs reconstructed" << std::endl;
                        // std::cout << pos2 << std::endl;

                        // print_transform(pos);

                        for (Move move2 : EGMoveList<REVERSE>(pos)) {
                            pos.move_piece(move2.from_sq(), move2.to_sq());
                            pos.flip_side_to_move();
                            // print_transform(pos);

                            uint64_t maybe_loss_ix = index.ix_from_pos(pos);

                            // std::cout << "2." << move_to_uci(move2, false) << " " << maybe_loss_ix << std::endl;
                            // std::cout << "Set level of " << maybe_loss_ix << " to " << -(LEVEL+1) << std::endl;
                            // std::cout << pos << std::endl;

                            // std::memset(&pos2, 0, sizeof(EGPosition));
                            // index.pos_at_ix(pos2, maybe_loss_ix);
                            // std::cout << "reconstructed " << maybe_loss_ix << std::endl;
                            // std::cout << pos2 << std::endl;

                            if (TB[maybe_loss_ix] == 0)
                                TB[maybe_loss_ix] = -(LEVEL+1); // mark as potential loss in LEVEL+1
                            pos.move_piece(move2.to_sq(), move2.from_sq());
                            pos.flip_side_to_move();
                        }
                    }
                    pos.move_piece(move.to_sq(), move.from_sq());
                    pos.flip_side_to_move();
                }
            }
        }
        std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;

        LEVEL++;

        N_LEVEL_POS = 0;
        for (uint64_t ix = 0; ix < NPOS; ix++) {
            if (TB[ix] == -LEVEL) { // potential loss in LEVEL
                std::memset(&pos, 0, sizeof(EGPosition));
                index.pos_at_ix(pos, ix);
                // std::cout << pos << std::endl;

                // check that all forward moves lead to checkmate in <= -(LEVEL-1)
                int16_t max_val = -1000;
                for (Move move : EGMoveList<FORWARD>(pos)) {
                    if (move.to_sq() & pos.pieces()) {
                        // capture
                        max_val = std::max(max_val, (int16_t) 0);
                        continue;
                    }

                    pos.move_piece(move.from_sq(), move.to_sq());
                    uint64_t fwd_ix = index.ix_from_pos(pos);
                    int16_t val = TB[fwd_ix];
                    max_val = std::max(max_val, val);
                    pos.move_piece(move.to_sq(), move.from_sq());
                }
                if (max_val > -(LEVEL - 1)) {
                    TB[ix] = 0;
                } else {
                    N_LEVEL_POS++;
                    // assert max_val == -(LEVEL - 1)
                    // TB[ix] = LEVEL
                }
            }
        }
        std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;

        iteration_counter++;
        if (iteration_counter >= 12) { break; }
        
    }



    return 0;
}