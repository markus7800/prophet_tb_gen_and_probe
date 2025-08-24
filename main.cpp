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
    Color stm = BLACK;

    PieceType pts[2] = {QUEEN, ROOK};
    Color cs[2] = {~stm, stm};


    std::vector<PieceType> stm_pieces = {};
    std::vector<PieceType> sntm_pieces = {QUEEN};
    KKXIndex index = KKXIndex(stm_pieces, sntm_pieces);

    EGPosition pos1;
    EGPosition pos2;
    EGPosition pos3;
    // for (Square p1_sq = SQ_A1; p1_sq <= SQ_H8; ++p1_sq) {
    //     for (Square p2_sq = SQ_A1; p2_sq <= SQ_H8; ++p2_sq) {
    //         if(p2_sq == p1_sq) { continue; }

    uint64_t count = 0;

    for (Square k1_sq = SQ_A1; k1_sq <= SQ_H8; ++k1_sq) {
        for (Square k2_sq = SQ_A1; k2_sq <= SQ_H8; ++k2_sq) {
            if (k2_sq == k1_sq) { continue; }
            for (Square p1_sq = SQ_A1; p1_sq <= SQ_H8; ++p1_sq) {
                if (p1_sq == k1_sq || p1_sq == k2_sq) { continue; }

                if ((PseudoAttacks[KING][k1_sq] & k2_sq) == 0) {
                    std::memset(&pos1, 0, sizeof(EGPosition));
                    std::memset(&pos2, 0, sizeof(EGPosition));
                    std::memset(&pos3, 0, sizeof(EGPosition));
                    pos1.put_piece(make_piece(cs[0],pts[0]), p1_sq);
                    // pos1.put_piece(make_piece(cs[1],pts[1]), p2_sq);
                    pos1.put_piece(B_KING, k1_sq);
                    pos1.put_piece(W_KING, k2_sq);
                    pos1.set_side_to_move(stm);

                    uint64_t ix = index.ix_from_pos(pos1);
                    index.pos_at_ix(pos2, ix, stm);
                    transform_to(pos1, pos3);
                    uint64_t ix2 = index.ix_from_pos(pos3);

                    if (!pos2.is_equal(pos3) || ix != ix2) {
                        std::cout << pos1 << std::endl;
                        std::cout << "vs at ix " << ix << std::endl;
                        std::cout << pos2 << std::endl;
                        std::cout << "vs transformed at ix " << ix2 << std::endl;
                        std::cout << pos3 << std::endl;
                        exit(1);
                    }
                    count++;
                }
            }
        }
    }
    std::cout << "Checked " << count << " kkx positions" << std::endl;
    
}

inline int16_t WIN_IN(int16_t level) { return 1000 - level; }
inline int16_t LOSS_IN(int16_t level) { return -1000 + level; }
#define MAYBELOSS -1001
#define UNUSEDIX -1002

int16_t ab(EGPosition &pos, int16_t alpha, int16_t beta, int ply) {
    if (ply == 10) {
        return 0;
    }
    if (pos.is_legal_checkmate()) {
        return LOSS_IN(ply);
    }
    for (Move move : EGMoveList<FORWARD>(pos)) {
        if (move.to_sq() & pos.pieces()) {
            // capture
            return 0;
        }

        pos.move_piece(move.from_sq(), move.to_sq());
        pos.flip_side_to_move();
        
        int16_t val = -ab(pos, -beta, -alpha, ply+1);

        pos.move_piece(move.to_sq(), move.from_sq());
        pos.flip_side_to_move();

        alpha = std::max(val, alpha);

        if (val >= beta) {
            alpha = beta;
            break;
        }


    }
    return alpha;
}

void print_pos_and_moves(EGPosition &pos, KKXIndex &index, int16_t* TB, KKXIndex &index2, int16_t* TB2) {
    uint64_t ix = index.ix_from_pos(pos);
    std::cout << pos << pos.fen() << " at ix: " << ix << " with val:" << int(TB[ix]) << std::endl;
    int16_t max_val = LOSS_IN(0);
    for (Move move : EGMoveList<FORWARD>(pos)) {
        if (move.to_sq() & pos.pieces()) {
            max_val = std::max(max_val, (int16_t) 0);
            std::cout << move_to_uci(move, false) << " capture" << std::endl;
            continue;
        }

        pos.move_piece(move.from_sq(), move.to_sq());
        pos.flip_side_to_move();

        uint64_t fwd_ix = index2.ix_from_pos(pos);
        int16_t val = TB2[fwd_ix];
        max_val = std::max(max_val, (int16_t) -TB2[fwd_ix]);
        std::cout << move_to_uci(move, false) << " " << val  << " at ix: " << fwd_ix << std::endl;

        pos.move_piece(move.to_sq(), move.from_sq());
        pos.flip_side_to_move();
    }
    std::cout << "max_val: " << max_val << std::endl;
}
void print_pos_and_rev_moves(EGPosition &pos, KKXIndex &index, int16_t* TB, KKXIndex &index2, int16_t* TB2) {
    uint64_t ix = index.ix_from_pos(pos);
    std::cout << pos << pos.fen() << " at ix: " << ix << " with val:" << int(TB[ix]) << std::endl;
    for (Move move : EGMoveList<REVERSE>(pos)) {

        pos.move_piece(move.from_sq(), move.to_sq());
        pos.flip_side_to_move();

        uint64_t fwd_ix = index2.ix_from_pos(pos);
        int16_t val = TB2[fwd_ix];
        std::cout << "rev: " << move_to_uci(move, false) << " " << val  << " at ix: " << fwd_ix << std::endl;

        pos.move_piece(move.to_sq(), move.from_sq());
        pos.flip_side_to_move();
    }
}
void check_consistency(EGPosition &pos, KKXIndex &index, int16_t* TB, KKXIndex &index2, int16_t* TB2) {
    uint64_t ix = index.ix_from_pos(pos);
    int16_t tb_val = TB[ix];
    int16_t val = LOSS_IN(0);
    for (Move move : EGMoveList<FORWARD>(pos)) {
        if (move.to_sq() & pos.pieces()) {
            val = std::max(val, (int16_t) 0);
            continue;
        }

        pos.move_piece(move.from_sq(), move.to_sq());
        pos.flip_side_to_move();

        uint64_t fwd_ix = index2.ix_from_pos(pos);
        val = std::max(val, (int16_t) -TB2[fwd_ix]);
        
        pos.move_piece(move.to_sq(), move.from_sq());
        pos.flip_side_to_move();
    }
    if (val > 0) { val--; }
    if (val < 0 && val != LOSS_IN(0)) { val++; }

    if (val != tb_val) {
        std::cout << "INCONSISTENCY: at ix " << ix << " " << val << " vs " << tb_val << std::endl;
        print_pos_and_moves(pos, index, TB, index2, TB2);
        exit(1);
    }
}


void add_win_and_maybe_losses(EGPosition &pos, int16_t LEVEL, KKXIndex index, int16_t* TB, KKXIndex index2, int16_t* TB2, uint64_t* N_LEVEL_POS) {
    uint64_t win_ix = index2.ix_from_pos(pos);
    
    if (TB2[win_ix] == 0) {

        TB2[win_ix] = WIN_IN(LEVEL);
        if (*N_LEVEL_POS == 0) { std::cout << pos << pos.fen() << ", ix: " << win_ix << std::endl; }
        (*N_LEVEL_POS)++;
        // return ;

        for (Move move2 : EGMoveList<REVERSE>(pos)) {
            pos.move_piece(move2.from_sq(), move2.to_sq());
            pos.flip_side_to_move();

            uint64_t maybe_loss_ix = index.ix_from_pos(pos);

            Square wk = pos.square<KING>(WHITE);
            Square bk = pos.square<KING>(BLACK);
            if (rank_of(wk) == file_of(wk) && rank_of(bk) == file_of(bk)) {
                pos.flip_diagonally();
                uint64_t flipped_maybe_loss_ix = index.ix_from_pos(pos);
                if (TB[flipped_maybe_loss_ix] == 0)
                    TB[flipped_maybe_loss_ix] = MAYBELOSS; // mark as potential loss in LEVEL+1
                pos.flip_diagonally();
            }

            if (TB[maybe_loss_ix] == 0)
                TB[maybe_loss_ix] = MAYBELOSS; // mark as potential loss in LEVEL+1
            
            pos.move_piece(move2.to_sq(), move2.from_sq());
            pos.flip_side_to_move();
        }
    }
}

void add_wins(EGPosition &pos, int16_t LEVEL, KKXIndex index, int16_t* TB, KKXIndex index2, int16_t* TB2, uint64_t* N_LEVEL_POS) {
    for (Move move : EGMoveList<REVERSE>(pos)) {
        // make reverse move and mark position as win in LEVEL
        pos.move_piece(move.from_sq(), move.to_sq()); // non-capture
        pos.flip_side_to_move();

        add_win_and_maybe_losses(pos, LEVEL, index, TB, index2, TB2, N_LEVEL_POS);
        
        Square wk = pos.square<KING>(WHITE);
        Square bk = pos.square<KING>(BLACK);
        if (rank_of(wk) == file_of(wk) && rank_of(bk) == file_of(bk)) {
            pos.flip_diagonally();
            add_win_and_maybe_losses(pos, LEVEL, index, TB, index2, TB2, N_LEVEL_POS);
            pos.flip_diagonally();
        }

        pos.move_piece(move.to_sq(), move.from_sq());
        pos.flip_side_to_move();
    }
}

int main() {
    Stockfish::Bitboards::init();
    Stockfish::Position::init();
    // Benchmark::perft("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 7, false);
    // check_transforms();
    // _enumerate_kk(0);

    Stockfish::enumerate_kk_naive();

    init_kkx_index();

    test_kkx_index();
    // return 0;

    std::vector<PieceType> stm_pieces = {};
    std::vector<PieceType> sntm_pieces = {QUEEN};
    KKXIndex index = KKXIndex(stm_pieces, sntm_pieces);
    KKXIndex index2 = KKXIndex(sntm_pieces, stm_pieces);
    uint64_t NPOS = index.num_positions();
    int16_t* TB = (int16_t*) calloc(sizeof(int16_t), NPOS);
    int16_t* TB2 = (int16_t*) calloc(sizeof(int16_t), NPOS);
    std::cout << "Num positions: " << index.num_positions() << std::endl;

    uint64_t count = 0;
    uint64_t matches = 0;
    EGPosition pos;


    // std::memset(&pos, 0, sizeof(EGPosition));
    // index.pos_at_ix(pos, 7278, BLACK);
    // print_pos_and_rev_moves(pos, index, TB, index2, TB2);

    // std::memset(&pos, 0, sizeof(EGPosition));
    // index2.pos_at_ix(pos, 7278, WHITE);
    // print_pos_and_rev_moves(pos, index2, TB2, index, TB);

    // return 0;

    TimePoint t0 = now();
    for (uint64_t ix = 0; ix < index.num_positions(); ix++) {

        std::memset(&pos, 0, sizeof(EGPosition));
        index.pos_at_ix(pos, ix, BLACK);
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
        
    }

    std::cout << "Matches count: " << matches << std::endl;
    std::cout << "Checkmate count: " << count << std::endl;

    TimePoint t1 = now();
    std::cout << "Finished in " << (t1-t0) / 1000.0 << std::endl;



    // int16_t* GT_TB = (int16_t*) calloc(sizeof(int16_t), NPOS);

    uint64_t N_UNUSED = 0;
    // init
    int16_t LEVEL = 0;
    uint64_t N_LEVEL_POS = 0;
    for (uint64_t ix = 0; ix < NPOS; ix++) {
        std::memset(&pos, 0, sizeof(EGPosition));
        index.pos_at_ix(pos, ix, BLACK);
        if (index.ix_from_pos(pos) != ix) {
            TB[ix] = UNUSEDIX;
            N_UNUSED++;
            continue;
        }
        if (pos.is_legal_checkmate()) {
            TB[ix] = LOSS_IN(LEVEL);
            N_LEVEL_POS++;
        }
    }
    

    std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;
    std::cout << N_UNUSED << " unused" << std::endl;

    uint64_t iteration_counter = 0;
    while (N_LEVEL_POS > 0) {
        LEVEL++;
        N_LEVEL_POS = 0;
        for (uint64_t ix = 0; ix < NPOS; ix++) {
            // for all checkmate in LEVEL-1
            if (TB[ix] == LOSS_IN(LEVEL-1)) {
                std::memset(&pos, 0, sizeof(EGPosition));
                index.pos_at_ix(pos, ix, BLACK);

                for (Move move : EGMoveList<REVERSE>(pos)) {
                    // make reverse move and mark position as win in LEVEL
                    pos.move_piece(move.from_sq(), move.to_sq()); // non-capture
                    pos.flip_side_to_move();

                    uint64_t win_ix = index2.ix_from_pos(pos);
    
                    if (TB2[win_ix] == 0) {
                        TB2[win_ix] = WIN_IN(LEVEL);

                        if (N_LEVEL_POS == 0) { std::cout << pos.fen() << ", ix: " << win_ix << std::endl; }
                        (N_LEVEL_POS)++;

                        for (Move move2 : EGMoveList<REVERSE>(pos)) {
                            pos.move_piece(move2.from_sq(), move2.to_sq());
                            pos.flip_side_to_move();

                            uint64_t maybe_loss_ix = index.ix_from_pos(pos);
                            if (TB[maybe_loss_ix] == 0) {
                                TB[maybe_loss_ix] = MAYBELOSS; // mark as potential loss in LEVEL+1
                            }
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
            // if (TB[ix] == MAYBELOSS) {
            if (TB[ix] == 0 || TB[ix] == MAYBELOSS) {
                std::memset(&pos, 0, sizeof(EGPosition));
                index.pos_at_ix(pos, ix, BLACK);

                // check that all forward moves lead to checkmate in <= -(LEVEL-1)
                EGMoveList moveList = EGMoveList<FORWARD>(pos);
                int16_t max_val = LOSS_IN(0);
                if (moveList.size() == 0) {
                    max_val = 0; // has to be stalemate, but should never happen since pos was found by reverse move
                    // std::cout << "stalemate?!:" << pos <<  ix << std::endl;
                } else {
                    for (Move move : moveList) {
                        if (move.to_sq() & pos.pieces()) {
                            // capture
                            max_val = std::max(max_val, (int16_t) 0);
                            break;
                        }

                        pos.move_piece(move.from_sq(), move.to_sq());
                        pos.flip_side_to_move();

                        uint64_t fwd_ix = index2.ix_from_pos(pos);
                        int16_t val = TB2[fwd_ix];
                        max_val = std::max(max_val, (int16_t) -val);
                        pos.move_piece(move.to_sq(), move.from_sq());
                        pos.flip_side_to_move();
                    }
                }
                if (max_val >= 0) {
                    TB[ix] = 0;
                } else {
                    if (N_LEVEL_POS == 0) { std::cout << pos.fen() << ", ix: " << ix << std::endl; }
                    N_LEVEL_POS++;
                    if (TB[ix] != MAYBELOSS) {
                        std::cout << "Missed loss at ix " << ix << " at level " << LEVEL << " " << int(TB[ix]) << std::endl;
                        print_pos_and_moves(pos, index, TB, index2, TB2);
                        exit(1);
                    }
                    TB[ix] = LOSS_IN(LEVEL);
                }
            }
        }
        std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;

        iteration_counter++;
        // if (iteration_counter >= 1) { break; }
        
    }

    // std::memset(&pos, 0, sizeof(EGPosition));
    // index.pos_at_ix(pos, 10381, BLACK);
    // print_pos_and_rev_moves(pos, index, TB, index2, TB2);

    // std::memset(&pos, 0, sizeof(EGPosition));
    // index2.pos_at_ix(pos, 11066, WHITE);
    // print_pos_and_rev_moves(pos, index2, TB2, index, TB);

    LEVEL = 0;
    while (true) {
        std::cout << "Check consistency at level " << LEVEL << std::endl;
        N_LEVEL_POS = 0;
        for (uint64_t ix = 0; ix < NPOS; ix++) {
            std::memset(&pos, 0, sizeof(EGPosition));
            index.pos_at_ix(pos, ix, BLACK);
            if (!pos.sntm_in_check() && TB[ix] == LOSS_IN(LEVEL)) {
                N_LEVEL_POS++;
                check_consistency(pos, index, TB, index2, TB2);
            }

            std::memset(&pos, 0, sizeof(EGPosition));
            index2.pos_at_ix(pos, ix, WHITE);
            if (!pos.sntm_in_check() && TB2[ix] == WIN_IN(LEVEL)) {
                N_LEVEL_POS++;
                check_consistency(pos, index2, TB2, index, TB);
            }

        }

        if (N_LEVEL_POS == 0) { break; }
        LEVEL++;

    }


    return 0;
}