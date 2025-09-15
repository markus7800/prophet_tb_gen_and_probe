
#ifndef GEN_EGTB_H_INCLUDED
#define GEN_EGTB_H_INCLUDED

#include "kkx.h"
#include "linearize.h"
#include "eg_position.h"
#include "eg_movegen.h"
#include "triangular_indexes.h"
#include "uci.h"
#include "misc.h"
#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>

uint64_t compute_num_positions(const int stm_pieces[6], const int sntm_pieces[6]) {
    int n_pawns = stm_pieces[PAWN] + sntm_pieces[PAWN];
    if (n_pawns == 0) {
        uint64_t n = N_KKX;
        uint64_t s = 62;
        
        for (int stm = 0; stm <= 1; ++stm) {
            const int* pieces = (stm) ? stm_pieces : sntm_pieces;
            for (PieceType i = KNIGHT; i < KING; ++i) {
                n *= number_of_ordered_tuples(s, pieces[i]);
                s -= pieces[i];
            }
        }

        return n;

    } else {
        uint64_t n = 1;
        uint64_t s = 64;

        // pawns
        uint64_t sp = 48;
        bool first_pawn = true;
        if (sntm_pieces[PAWN] > 0) {
            n *= number_of_ordered_tuples_with_first_pawn(sntm_pieces[PAWN]);
            s -= sntm_pieces[PAWN];
            sp -= sntm_pieces[PAWN];
            first_pawn = false;
        }
        if (stm_pieces[PAWN] > 0) {
            if (first_pawn) {
                n *= number_of_ordered_tuples_with_first_pawn(stm_pieces[PAWN]);
            } else {
                n *= number_of_ordered_tuples(sp, stm_pieces[PAWN]);
            }
            s -= stm_pieces[PAWN];
        }


        // kings
        n *= s;
        s--;
        n *= s;
        s--;

        for (int stm = 0; stm <= 1; ++stm) {
            const int* pieces = (stm) ? stm_pieces : sntm_pieces;
            for (PieceType i = KNIGHT; i < KING; ++i) {
                n *= number_of_ordered_tuples(s, pieces[i]);
                s -= pieces[i];
            }
        }

        return n;
    }
}
std::string get_pieces_identifier(int pieces[6]) {
    std::ostringstream os;
    os << "K";
    for (PieceType pt = QUEEN; pt >= PAWN; --pt) {
        for (int i = 0; i < pieces[pt]; i++) {
            os << PieceToChar[pt];
        }
    }
    return os.str();
}

std::string get_egtb_identifier(int stm_pieces[6], int sntm_pieces[6]) {
    std::ostringstream os;
    for (int* pieces: {stm_pieces, sntm_pieces}) {
        os << get_pieces_identifier(pieces);
    }
    return os.str();
}

std::string get_filename(int stm_pieces[6], int sntm_pieces[6]) {
    std::ostringstream os;
    os << "egtbs/";
    os << get_egtb_identifier(stm_pieces, sntm_pieces);
    os << ".egtb";
    return os.str();
}


void store_egtb(int16_t* TB, int stm_pieces[6], int sntm_pieces[6]) {
    uint64_t NPOS = compute_num_positions(stm_pieces, sntm_pieces);
    std::string filename = get_filename(stm_pieces, sntm_pieces);
    std::ofstream outputFileStream;
    outputFileStream.open(filename, std::ios::out|std::ios::binary);
    for(uint64_t i=0; i<NPOS; i++)
        outputFileStream.write((char*) &TB[i], sizeof(int16_t));
}

void load_egtb(int16_t* TB, int stm_pieces[6], int sntm_pieces[6]) {
    uint64_t NPOS = compute_num_positions(stm_pieces, sntm_pieces);
    std::string filename = get_filename(stm_pieces, sntm_pieces);
    std::ifstream inputFileStream;
    inputFileStream.open(filename, std::ios::in|std::ios::binary);
    for(uint64_t i=0; i<NPOS; i++)
        inputFileStream.read((char*) &TB[i], sizeof(int16_t));
}

bool egtb_exists(int stm_pieces[6], int sntm_pieces[6]) {
    std::string filename = get_filename(stm_pieces, sntm_pieces);
    std::ifstream inputFileStream = std::ifstream(filename);
    return inputFileStream.good();
}

class GenEGTB {
    int wpieces[6];
    int bpieces[6];
    int n_pieces;

    int16_t* WTM_TB;
    int16_t* BTM_TB;

    int16_t* WTM_TBs[6][6];

    int16_t* BTM_TBs[6][6];

    uint64_t WTM_TBs_NPOS[6][6];
    uint64_t BTM_TBs_NPOS[6][6];

public:
    GenEGTB(int wpieces[6], int bpieces[6]) {
        // std::cout << "GenEGTB\n";
        this->n_pieces = 0;
        for (int i = 0; i < 6; i++) {
            this->wpieces[i] = wpieces[i];
            this->bpieces[i] = bpieces[i];
            this->n_pieces += wpieces[i] + bpieces[i];
        }

        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < 6; j++) {
                this->WTM_TBs[i][j] = NULL;
                this->BTM_TBs[i][j] = NULL;
            }
        }

        uint64_t NPOS = num_positions();
        this->WTM_TB = (int16_t*) calloc(sizeof(int16_t), NPOS);
        this->BTM_TB = (int16_t*) calloc(sizeof(int16_t), NPOS);


        std::cout << "White pieces: " << get_pieces_identifier(wpieces) << std::endl;
        std::cout << "Black pieces: " << get_pieces_identifier(bpieces) << std::endl;

        this->WTM_TBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = WTM_TB;
        this->BTM_TBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = BTM_TB;
        this->WTM_TBs_NPOS[NO_PIECE_TYPE][NO_PIECE_TYPE] = NPOS;
        this->BTM_TBs_NPOS[NO_PIECE_TYPE][NO_PIECE_TYPE] = NPOS;

        // captures
        for (PieceType capture_pt = PAWN; capture_pt <= QUEEN; ++capture_pt) {
            if (wpieces[capture_pt] > 0) {
                wpieces[capture_pt]--;
                uint64_t n = compute_num_positions(wpieces, bpieces);
                this->WTM_TBs_NPOS[NO_PIECE_TYPE][capture_pt] = n;
                this->WTM_TBs[NO_PIECE_TYPE][capture_pt] = (int16_t*) calloc(sizeof(int16_t), n);
                load_egtb(this->WTM_TBs[NO_PIECE_TYPE][capture_pt], wpieces, bpieces);
                std::cout << "Load " << get_egtb_identifier(wpieces, bpieces) << " for white " << PieceToChar[capture_pt] << " captured, white to move" << std::endl;
                wpieces[capture_pt]++;
            }
            if (bpieces[capture_pt] > 0) {
                bpieces[capture_pt]--;
                uint64_t n = compute_num_positions(wpieces, bpieces);
                this->BTM_TBs_NPOS[NO_PIECE_TYPE][capture_pt] = n;
                this->BTM_TBs[NO_PIECE_TYPE][capture_pt] = (int16_t*) calloc(sizeof(int16_t), n);
                load_egtb(this->BTM_TBs[NO_PIECE_TYPE][capture_pt], bpieces, wpieces); 
                std::cout << "Load " << get_egtb_identifier(bpieces, wpieces) << " for black " << PieceToChar[capture_pt] << " captured, black to move"  << std::endl;
                bpieces[capture_pt]++;
            }
        }

        // promotions
        for (PieceType promote_pt = KNIGHT; promote_pt <= QUEEN; ++promote_pt) {
            if (bpieces[PAWN] > 0) {
                bpieces[PAWN]--;
                bpieces[promote_pt]++;
                uint64_t n = compute_num_positions(wpieces, bpieces);
                this->WTM_TBs_NPOS[promote_pt][NO_PIECE_TYPE] = n;
                this->WTM_TBs[promote_pt][NO_PIECE_TYPE] = (int16_t*) calloc(sizeof(int16_t), n);
                load_egtb(this->WTM_TBs[promote_pt][NO_PIECE_TYPE], wpieces, bpieces);
                std::cout << "Load " << get_egtb_identifier(wpieces, bpieces) << " for black promotion to " << PieceToChar[promote_pt] << ", white to move" << std::endl;
                bpieces[PAWN]++;
                bpieces[promote_pt]--;
            }
            if (wpieces[PAWN] > 0) {
                wpieces[PAWN]--;
                wpieces[promote_pt]++;
                uint64_t n = compute_num_positions(wpieces, bpieces);
                this->BTM_TBs_NPOS[promote_pt][NO_PIECE_TYPE] = n;
                this->BTM_TBs[promote_pt][NO_PIECE_TYPE] = (int16_t*) calloc(sizeof(int16_t), n);
                load_egtb(this->BTM_TBs[promote_pt][NO_PIECE_TYPE], bpieces, wpieces); 
                std::cout << "Load " << get_egtb_identifier(bpieces, wpieces) << " for white promotion to " << PieceToChar[promote_pt] << ", black to move" << std::endl;
                wpieces[PAWN]++;
                wpieces[promote_pt]--;
            }
        }

        // capture promotions
        for (PieceType promote_pt = KNIGHT; promote_pt <= QUEEN; ++promote_pt) {
            for (PieceType capture_pt = KNIGHT; capture_pt <= QUEEN; ++capture_pt) {
                if (bpieces[PAWN] > 0 && wpieces[capture_pt] > 0) {
                    wpieces[capture_pt]--;
                    bpieces[PAWN]--;
                    bpieces[promote_pt]++;
                    uint64_t n = compute_num_positions(wpieces, bpieces);
                    this->WTM_TBs_NPOS[promote_pt][capture_pt] = n;
                    this->WTM_TBs[promote_pt][capture_pt] = (int16_t*) calloc(sizeof(int16_t), n);
                    load_egtb(this->WTM_TBs[promote_pt][capture_pt], wpieces, bpieces);
                    std::cout << "Load " << get_egtb_identifier(wpieces, bpieces) << " for white " << PieceToChar[capture_pt] << " captured with black promotion to " << PieceToChar[promote_pt] << ", white to move" << std::endl;
                    wpieces[capture_pt]++;
                    bpieces[PAWN]++;
                    bpieces[promote_pt]--;
                }
                if (wpieces[PAWN] > 0 && bpieces[capture_pt] > 0) {
                    bpieces[capture_pt]--;
                    wpieces[PAWN]--;
                    wpieces[promote_pt]++;
                    uint64_t n = compute_num_positions(wpieces, bpieces);
                    this->BTM_TBs_NPOS[promote_pt][capture_pt] = n;
                    this->BTM_TBs[promote_pt][capture_pt] = (int16_t*) calloc(sizeof(int16_t), n);
                    load_egtb(this->BTM_TBs[promote_pt][capture_pt], bpieces, wpieces); 
                    std::cout << "Load " << get_egtb_identifier(bpieces, wpieces) << " for black " << PieceToChar[capture_pt] << " captured with white promotion to " << PieceToChar[promote_pt] << ", black to move" << std::endl;
                    bpieces[capture_pt]++;
                    wpieces[PAWN]++;
                    wpieces[promote_pt]--;
                }
            }
        }
        
    }
    ~GenEGTB() {
        // std::cout << "~GenEGTB\n";
        // is freed below
        // free(WTM_TB);
        // free(BTM_TB);

        for (PieceType promotion_pt = NO_PIECE_TYPE; promotion_pt <= QUEEN; ++promotion_pt) {
            for (PieceType capture_pt = NO_PIECE_TYPE; capture_pt <= QUEEN; ++capture_pt) {
                if (WTM_TBs[promotion_pt][capture_pt] != NULL) {
                    free(WTM_TBs[promotion_pt][capture_pt]);
                }
                if (BTM_TBs[promotion_pt][capture_pt] != NULL) {
                    free(BTM_TBs[promotion_pt][capture_pt]);
                }
            }
        }
    }


    int num_pieces() const;
    uint64_t num_positions() const;
    void gen();
    void check_consistency(EGPosition &pos, bool verbose);
};

int GenEGTB::num_pieces() const {
    return n_pieces;
}


uint64_t GenEGTB::num_positions() const {
    return compute_num_positions(wpieces, bpieces);
}



inline int16_t WIN_IN(int16_t level) { return 1000 - level; }
inline int16_t LOSS_IN(int16_t level) { return -1000 + level; }
#define MAYBELOSS -1001
#define UNUSEDIX -1002
inline int16_t IS_SET(int16_t val) { return val != 0 && LOSS_IN(0) <= val && val <= WIN_IN(0); }
inline int16_t IS_UNSET(int16_t val) { return val == 0; } //  || val == MAYBELOSS


void GenEGTB::check_consistency(EGPosition &pos, bool verbose) {
    assert (!pos.sntm_in_check());
    int16_t* TB = pos.side_to_move() == WHITE ? WTM_TB : BTM_TB;
    int16_t* (*CAPTURE_TBs)[6] = pos.side_to_move() == WHITE ? BTM_TBs : WTM_TBs;

    if (verbose) std::cout << pos << pos.fen() << std::endl;

    uint64_t ix = ix_from_pos(pos);
    int16_t tb_val = TB[ix];

    int16_t max_val = LOSS_IN(0);
    int16_t val;
    EGMoveList movelist = EGMoveList<FORWARD>(pos);
    for (Move move : movelist) {
        PieceType capture = pos.do_move(move);
        PieceType promotion = move.type_of() == PROMOTION ? move.promotion_type() : NO_PIECE_TYPE;
        uint64_t fwd_ix = ix_from_pos(pos);
        // std::cout << PieceToChar[capture] << " - " << PieceToChar[promotion] << std::endl;
        val = CAPTURE_TBs[promotion][capture][fwd_ix];
        if (capture) {
            if (verbose) std::cout << "  " << move_to_uci(move) << "x " << val << " at ix: " << fwd_ix << std::endl;
        } else {
            if (verbose) std::cout << "  " << move_to_uci(move) << " " << val << " at ix: " << fwd_ix << std::endl;
        }
        max_val = std::max(max_val, (int16_t) -val);

        pos.undo_move(move, capture);
    }
    if (movelist.size() == 0) {
        if (pos.stm_in_check()) {
            max_val = LOSS_IN(0); // checkmate
            if (verbose) std::cout << "  pos is checkmate" << std::endl;
        }
        else {
            max_val = 0; // stale-mate
            if (verbose) std::cout << "  pos is stalemate" << std::endl;
        }
    } else {
        if (max_val > 0) { max_val--; }
        if (max_val < 0) { max_val++; }
    }

    if (max_val != tb_val) {
        std::cout << "INCONSISTENCY: at ix " << ix << " max_val: " << max_val << " vs tb_val:" << tb_val << std::endl;
        if (!verbose)
            check_consistency(pos, true);
        exit(1);
    }
}

void GenEGTB::gen() {
    std::cout << "Generate " << get_egtb_identifier(wpieces, bpieces) << " and " << get_egtb_identifier(bpieces, wpieces) << std::endl;

    if (egtb_exists(wpieces, bpieces)) {
        std::cout << "Already exists." << std::endl;
        return;
    }


    uint64_t NPOS = num_positions();
    std::cout << "Number of positions: " << NPOS << std::endl;

    EGPosition pos;

    int16_t LEVEL = 0;

    uint64_t N_LEVEL_POS = 0;

    int16_t* LOSS_TB;
    int16_t* WIN_TB;
    int16_t* (*LOSS_TBs)[6];
    uint64_t (*LOSS_TBs_NPOS)[6];
    int16_t* (*CAPTURE_TBs)[6];
    Color LOSS_COLOR;

    TimePoint t0 = now();

    for (int wtm = 0; wtm <= 1; ++wtm) {
        if (wtm) {
            LOSS_TB = WTM_TB;
            LOSS_COLOR = WHITE;
            CAPTURE_TBs = BTM_TBs;
        } else {
            LOSS_TB = BTM_TB;
            LOSS_COLOR = BLACK;
            CAPTURE_TBs = WTM_TBs;
        }
        uint64_t N_UNUSED = 0;
        uint64_t N_CHECKMATE = 0;
        for (uint64_t ix = 0; ix < NPOS; ix++) {
            pos.reset();
            pos_at_ix(pos, ix, LOSS_COLOR, wpieces, bpieces);
            if (ix_from_pos(pos) != ix || pos.sntm_in_check()) {
                LOSS_TB[ix] = UNUSEDIX;
                N_UNUSED++;
                continue;
            }
            EGMoveList movelist = EGMoveList<FORWARD>(pos);
            if (movelist.size() == 0) {
                if (pos.stm_in_check()) {
                    LOSS_TB[ix] = LOSS_IN(0);
                    N_CHECKMATE++;
                    N_LEVEL_POS++;
                }
            } else {
                int16_t max_val = LOSS_IN(0);
                bool has_eval = true;
                for (Move move : movelist) {
                    PieceType capture = pos.do_move(move);
                    PieceType promotion = move.type_of() == PROMOTION ? move.promotion_type() : NO_PIECE_TYPE;
                    if (!capture && !promotion) {
                        // move stays in TB, cannot determine eval
                        has_eval = false;
                        break;
                    }
                    uint64_t fwd_ix = ix_from_pos(pos);
                    max_val = std::max(max_val, (int16_t) -CAPTURE_TBs[promotion][capture][fwd_ix]);
                    pos.undo_move(move, capture);
                }
                if (has_eval) {
                    // all moves lead to dependent TB (cannot be stalemate since we have moves)
                    if (max_val > 0) { max_val--; }
                    if (max_val < 0) { max_val++; }
                    LOSS_TB[ix] = max_val;
                    N_LEVEL_POS++;
                }
            }

        }
        std::cout << "Checkmate count: " << N_CHECKMATE << std::endl;
        std::cout << N_UNUSED << " unused indices" << std::endl;
    }

    int16_t MIN_LEVEL = 0;
    for (int wtm = 0; wtm <= 1; ++wtm) {
        if (wtm) {
            LOSS_COLOR = WHITE;
            LOSS_TB = WTM_TB;
            LOSS_TBs = WTM_TBs;
            LOSS_TBs_NPOS = WTM_TBs_NPOS;
            WIN_TB = BTM_TB;
        } else {
            LOSS_COLOR = BLACK;
            LOSS_TB = BTM_TB;
            LOSS_TBs = BTM_TBs;
            LOSS_TBs_NPOS = BTM_TBs_NPOS;
            WIN_TB = WTM_TB;
        }
        
        EGPosition transformed_pos;
        int8_t V_FLIPS[8] = {0,  0, 56, 56,  0,  0, 56, 56};
        int8_t H_FLIPS[8] = {0,  7,  0,  7,  0,  7,  0,  7};
        int8_t SWAPS[8]   = {0,  0,  0,  0,  3,  3,  3,  3};

        for (PieceType promotion_pt = NO_PIECE_TYPE; promotion_pt <= QUEEN; ++promotion_pt) {
            for (PieceType capture_pt = NO_PIECE_TYPE; capture_pt <= QUEEN; ++capture_pt) {
                if (promotion_pt == NO_PIECE_TYPE && capture_pt == NO_PIECE_TYPE) { continue; }
                if (LOSS_TBs[promotion_pt][capture_pt] == NULL) { continue; }
                std::cout << "Reverse from color=" << ((LOSS_COLOR == WHITE) ? "W" : "B")  << ", captured=" << PieceToChar[capture_pt] << ", promotion=" << PieceToChar[promotion_pt] << ", #positions=" << LOSS_TBs_NPOS[promotion_pt][capture_pt] << std::endl;

                if (capture_pt) {
                    if (LOSS_COLOR == WHITE)
                        wpieces[capture_pt]--;
                    else 
                        bpieces[capture_pt]--;
                }
                if (promotion_pt) {
                    if (LOSS_COLOR == WHITE) {
                        bpieces[PAWN]--;
                        bpieces[promotion_pt]++;
                    } else {
                        wpieces[PAWN]--;
                        wpieces[promotion_pt]++;
                    }
                }

                int N_SYMMETRIES = wpieces[PAWN] + bpieces[PAWN] > 0 ? 2 : 8;

                for (uint64_t ix = 0; ix < LOSS_TBs_NPOS[promotion_pt][capture_pt]; ix++) {
                    int16_t val = LOSS_TBs[promotion_pt][capture_pt][ix];
                    if (0 < val && val <= WIN_IN(0)) {
                        MIN_LEVEL = std::max(MIN_LEVEL, (int16_t) (WIN_IN(0) - val));
                    }
                    if (0 > val && val >= LOSS_IN(0)) {
                        MIN_LEVEL = std::max(MIN_LEVEL, (int16_t) (val - LOSS_IN(0)));

                        int16_t win_val = -val - 1;
                        pos.reset();
                        pos_at_ix(pos, ix, LOSS_COLOR, wpieces, bpieces);
                        assert (!pos.sntm_in_check());

                        

                        for (int t = 0; t < N_SYMMETRIES; t++) {
                            transformed_pos.reset();
                            transform_to(pos, transformed_pos, H_FLIPS[t], V_FLIPS[t], SWAPS[t]);

                            // if (promotion_pt == BISHOP && capture_pt == NO_PIECE_TYPE && ix == 96) {
                            //     std::cout << transformed_pos;
                            // }

                            for (Move move : EGMoveList<REVERSE>(transformed_pos, capture_pt, promotion_pt)) {
                                // if (promotion_pt == BISHOP && capture_pt == NO_PIECE_TYPE && ix == 96) {
                                //     std::cout << move_to_uci(move) << "x" << PieceToChar[capture_pt]  << " -> " << int(win_val) << std::endl;
                                // }
                                transformed_pos.do_rev_move(move, capture_pt);
                                uint64_t win_ix = ix_from_pos(transformed_pos);


                                if (IS_UNSET(WIN_TB[win_ix]) || WIN_TB[win_ix] < win_val ) {
                                    // assert (!transformed_pos.sntm_in_check());
                                    WIN_TB[win_ix] = win_val;
                                }
                                transformed_pos.undo_rev_move(move);
                            }

                        }

                    }
                }

                if (capture_pt) {
                    if (LOSS_COLOR == WHITE)
                        wpieces[capture_pt]++;
                    else 
                        bpieces[capture_pt]++;
                }
                if (promotion_pt) {
                    if (LOSS_COLOR == WHITE) {
                        bpieces[PAWN]++;
                        bpieces[promotion_pt]--;
                    } else {
                        wpieces[PAWN]++;
                        wpieces[promotion_pt]--;
                    }
                }
            }
        }
    }
    std::cout << "MIN_LEVEL = " << int(MIN_LEVEL) << std::endl;

    while (true) {
        LEVEL++;
        N_LEVEL_POS = 0;

        std::cout << LEVEL << " REVERSE GEN\n";

        for (int wtm = 0; wtm <= 1; ++wtm) {
            if (wtm) {
                LOSS_COLOR = WHITE;
                LOSS_TB = WTM_TB;
                WIN_TB = BTM_TB;
            } else {
                LOSS_COLOR = BLACK;
                LOSS_TB = BTM_TB;
                WIN_TB = WTM_TB;
            }
            assert (LOSS_TB != WIN_TB);

            for (uint64_t ix = 0; ix < NPOS; ix++) {
                if (LOSS_TB[ix] == LOSS_IN(LEVEL-1)) {
                    int16_t win_val = WIN_IN(LEVEL);
                    pos.reset();
                    pos_at_ix(pos, ix, LOSS_COLOR, wpieces, bpieces);
                    assert (!pos.sntm_in_check());
                    for (Move move : EGMoveList<REVERSE>(pos)) {
                        pos.do_rev_move(move);
                        uint64_t win_ix = ix_from_pos(pos);
                        if (IS_UNSET(WIN_TB[win_ix]) || WIN_TB[win_ix] < win_val ) {
                            WIN_TB[win_ix] = win_val;
                        }
                        pos.undo_rev_move(move);
                    }
                }
            }


            for (uint64_t win_ix = 0; win_ix < NPOS; win_ix++) {
                if (WIN_TB[win_ix] == WIN_IN(LEVEL)) {
                    pos.reset();
                    pos_at_ix(pos, win_ix, ~LOSS_COLOR, wpieces, bpieces); // not much slower
                    assert (!pos.sntm_in_check());
                    if (N_LEVEL_POS == 0) { std::cout << "WIN in " <<  LEVEL << ": " << pos.fen() << ", ix: " << win_ix << std::endl; }
                    N_LEVEL_POS++;

                    // if (win_ix == 3260853 && LOSS_TB == BTM_TB) { std::cout << pos << win_ix << std::endl; }

                    for (Move move : EGMoveList<REVERSE>(pos)) {
                        pos.do_rev_move(move); // no capture, stay in WIN_TB / LOSS_TB config
                        uint64_t maybe_loss_ix = ix_from_pos(pos);
                        // if (win_ix == 3260853 && LOSS_TB == BTM_TB) { std::cout << move_to_uci(move) << " " << maybe_loss_ix << "\n"; }
                        if (IS_UNSET(LOSS_TB[maybe_loss_ix])) {
                            LOSS_TB[maybe_loss_ix] = MAYBELOSS;
                            // if (maybe_loss_ix == 4458573 && LOSS_TB == BTM_TB) {
                            //     std::cout << "HERE win_ix=" << win_ix << std::endl;
                            //     std::cout << pos;
                            // }
                        }
                        pos.undo_rev_move(move);
                    }
                }
            }
            
        }


        std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;

        std::cout << LEVEL << " FORWARD GEN\n";

        LEVEL++;
        N_LEVEL_POS = 0;
        for (int wtm = 0; wtm <= 1; ++wtm) {
            if (wtm) {
                LOSS_TB = WTM_TB;
                LOSS_COLOR = WHITE;
                CAPTURE_TBs = BTM_TBs;
                WIN_TB = BTM_TB;
            } else {
                LOSS_TB = BTM_TB;
                LOSS_COLOR = BLACK;
                CAPTURE_TBs = WTM_TBs;
                WIN_TB = WTM_TB;
            }
            assert (CAPTURE_TBs[NO_PIECE_TYPE][NO_PIECE_TYPE] == WIN_TB);

            for (uint64_t ix = 0; ix < NPOS; ix++) {
                if (LOSS_TB[ix] == MAYBELOSS) {
                // if (LOSS_TB[ix] == MAYBELOSS || LOSS_TB[ix] == 0) {
                    pos.reset();
                    pos_at_ix(pos, ix, LOSS_COLOR, wpieces, bpieces);
                    assert (!pos.sntm_in_check()); // should be UNUSED_IX if sntm_in_check

                    // check that all forward moves lead to checkmate in <= -(LEVEL-1)
                    EGMoveList moveList = EGMoveList<FORWARD>(pos);
                    int16_t max_val = LOSS_IN(0);
                    if (moveList.size() == 0) {
                        max_val = 0; // has to be stale mate
                        if (LOSS_TB[ix] == MAYBELOSS)
                            std::cout << pos << ix << std::endl;
                        assert(LOSS_TB[ix] != MAYBELOSS); // cannot happen at MAYBELOSS position
                    } else {
                        for (Move move : moveList) {
                            PieceType capture = pos.do_move(move);
                            PieceType promotion = move.type_of() == PROMOTION ? move.promotion_type() : NO_PIECE_TYPE;

                            uint64_t fwd_ix = ix_from_pos(pos);

                            int16_t val = -CAPTURE_TBs[promotion][capture][fwd_ix]; // CAPTURE_TBs[0][0] = WIN_TB
                            max_val = std::max(max_val, val);
                            pos.undo_move(move, capture);
                        }
                    }
                    if (max_val >= 0) {
                        LOSS_TB[ix] = 0;
                    } else {
                        if (N_LEVEL_POS == 0) { std::cout << "LOSS in " <<  LEVEL << ": " << pos.fen() << ", ix: " << ix << std::endl; }
                        N_LEVEL_POS++;

                        if (LOSS_TB[ix] != MAYBELOSS) {
                            std::cout << "Missed loss with " << LOSS_TB[ix] << "\n" << pos;
                            exit(1);
                        }

                        if (max_val == LOSS_IN(LEVEL-1)) {
                            LOSS_TB[ix] = LOSS_IN(LEVEL);
                        }
                    }

                }
            }
        }

        std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;

        if (LEVEL > MIN_LEVEL && N_LEVEL_POS == 0) { break; }
    }


    TimePoint t1 = now();
    std::cout << "Finished in " << (double) (t1 - t0) / 1000.0 << "s." << std::endl;

    // Consistency checks:

    LEVEL = 0;
    N_LEVEL_POS = 0;
    while (true) {
        for (uint64_t ix = 0; ix < NPOS; ix++) {
            if (WTM_TB[ix] == LOSS_IN(LEVEL) || WTM_TB[ix] == WIN_IN(LEVEL)) {
                pos.reset();
                pos_at_ix(pos, ix, WHITE, wpieces, bpieces);
                assert (!pos.sntm_in_check());
                check_consistency(pos, false);
                N_LEVEL_POS++;
            }
            if (BTM_TB[ix] == LOSS_IN(LEVEL) || BTM_TB[ix] == WIN_IN(LEVEL)) {
                pos.reset();
                pos_at_ix(pos, ix, BLACK, wpieces, bpieces);
                assert (!pos.sntm_in_check());
                check_consistency(pos, false);
                N_LEVEL_POS++;
            }
        }
        std::cout << "Consistency check passed for level " << LEVEL << std::endl;
        if (LEVEL > MIN_LEVEL && N_LEVEL_POS == 0) { break; }
        
        LEVEL++;
        N_LEVEL_POS = 0;
    }
    for (uint64_t ix = 0; ix < NPOS; ix++) {
        if (WTM_TB[ix] == 0) {
            pos.reset();
            pos_at_ix(pos, ix, WHITE, wpieces, bpieces);
            assert(!pos.sntm_in_check());
            check_consistency(pos, false);
        }
        if (BTM_TB[ix] == 0) {
            pos.reset();
            pos_at_ix(pos, ix, BLACK, wpieces, bpieces);
            assert(!pos.sntm_in_check());
            check_consistency(pos, false);
        }
    }
    std::cout << "Consistency check passed for draws" << std::endl;


    for (int wtm = 0; wtm <= 1; ++wtm) {
        int16_t* _TB = wtm ? WTM_TB : BTM_TB;
        std::string egtb = wtm ? get_egtb_identifier(wpieces, bpieces) : get_egtb_identifier(bpieces, wpieces);

        uint64_t wins = 0;
        uint64_t draws = 0;
        uint64_t losses = 0;
        for (uint64_t ix = 0; ix < NPOS; ix++) {
            if (_TB[ix] == UNUSEDIX) { continue; }
            wins += (_TB[ix] > 0);
            draws += (_TB[ix] == 0);
            losses += (_TB[ix] < 0);
        }
        std::cout << wins << " wins in " << egtb << std::endl;
        std::cout << draws << " draws in " << egtb << std::endl;
        std::cout << losses << " losses in " << egtb << std::endl;
    }

    store_egtb(WTM_TB, wpieces, bpieces);
    store_egtb(BTM_TB, bpieces, wpieces);
    std::cout << "\n\n" << std::endl;

}

#endif