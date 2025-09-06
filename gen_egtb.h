
#ifndef GEN_EGTB_H_INCLUDED
#define GEN_EGTB_H_INCLUDED

#include "kkx.h"
#include "linearize.h"
#include "eg_position.h"
#include "eg_movegen.h"
#include "uci.h"
#include "misc.h"
#include <iostream>
#include <fstream>
#include <string>

uint64_t compute_num_positions(const int stm_pieces[6], const int sntm_pieces[6]) {
    // TODO
    uint64_t n = N_KKX;
    
    int k = 0;
    for (int i = 0; i < 6; i++) {
        k += stm_pieces[i] + sntm_pieces[i];
    }

    uint64_t s = 62;
    for (int i = 0; i < k; i++) {
        n *= s;
        s--;
    }
    return n;
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


class GenEGTB {
    int wpieces[6];
    int bpieces[6];
    int n_pieces;

    int16_t* WTM_TB;
    int16_t* BTM_TB;

    int16_t* WPC_WTM_TBs[6];
    int16_t* WPC_BTM_TBs[6];

    int16_t* BPC_WTM_TBs[6];
    int16_t* BPC_BTM_TBs[6];

    uint64_t WPC_TBs_NPOS[6];
    uint64_t BPC_TBs_NPOS[6];

public:
    GenEGTB(int wpieces[6], int bpieces[6]) {
        this->n_pieces = 0;
        for (int i = 0; i < 6; i++) {
            this->wpieces[i] = wpieces[i];
            this->bpieces[i] = bpieces[i];
            this->n_pieces += wpieces[i] + bpieces[i];
        }

        uint64_t NPOS = num_positions();
        this->WTM_TB = (int16_t*) calloc(sizeof(int16_t), NPOS);
        this->BTM_TB = (int16_t*) calloc(sizeof(int16_t), NPOS);

        this->WPC_WTM_TBs[NO_PIECE_TYPE] = WTM_TB;
        this->BPC_WTM_TBs[NO_PIECE_TYPE] = WTM_TB;
        this->WPC_BTM_TBs[NO_PIECE_TYPE] = BTM_TB;
        this->BPC_BTM_TBs[NO_PIECE_TYPE] = BTM_TB;
        this->WPC_TBs_NPOS[NO_PIECE_TYPE] = NPOS;
        this->BPC_TBs_NPOS[NO_PIECE_TYPE] = NPOS;

        std::cout << "White pieces: " << get_pieces_identifier(wpieces) << std::endl;
        std::cout << "Black pieces: " << get_pieces_identifier(bpieces) << std::endl;

        for (PieceType pt = PAWN; pt <= QUEEN; ++pt) {
            if (wpieces[pt] > 0) {
                wpieces[pt]--;
                this->WPC_TBs_NPOS[pt] = compute_num_positions(wpieces, bpieces);
                this->WPC_WTM_TBs[pt] = (int16_t*) calloc(sizeof(int16_t), this->WPC_TBs_NPOS[pt]);
                load_egtb(this->WPC_WTM_TBs[pt], wpieces, bpieces);
                std::cout << "Load " << get_egtb_identifier(wpieces, bpieces) << " for white piece captured, white to move" << std::endl;
                this->WPC_BTM_TBs[pt] = (int16_t*) calloc(sizeof(int16_t), this->WPC_TBs_NPOS[pt]);
                load_egtb(this->WPC_BTM_TBs[pt], bpieces, wpieces);
                std::cout << "Load " << get_egtb_identifier(bpieces, wpieces) << " for white piece captured, black to move" << std::endl;
                wpieces[pt]++;
            } else {
                this->WPC_WTM_TBs[pt] = NULL;
                this->WPC_BTM_TBs[pt] = NULL;
            }
            if (bpieces[pt] > 0) {
                bpieces[pt]--;
                this->BPC_TBs_NPOS[pt] = compute_num_positions(wpieces, bpieces);
                this->BPC_WTM_TBs[pt] = (int16_t*) calloc(sizeof(int16_t), this->BPC_TBs_NPOS[pt]);
                load_egtb(this->BPC_WTM_TBs[pt], wpieces, bpieces); 
                std::cout << "Load " << get_egtb_identifier(wpieces, bpieces) << " for black piece captured, white to move"  << std::endl;
                this->BPC_BTM_TBs[pt] = (int16_t*) calloc(sizeof(int16_t), this->BPC_TBs_NPOS[pt]);
                load_egtb(this->BPC_BTM_TBs[pt], bpieces, wpieces); 
                std::cout << "Load " << get_egtb_identifier(bpieces, wpieces) << " for black piece captured, black to movee"  << std::endl;
                bpieces[pt]++;
            } else {
                this->BPC_WTM_TBs[pt] = NULL;
                this->BPC_BTM_TBs[pt] = NULL;
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


void GenEGTB::check_consistency(EGPosition &pos, bool verbose) {
    int16_t* TB = pos.side_to_move() == WHITE ? WTM_TB : BTM_TB;
    int16_t* MIRROR_TB = pos.side_to_move() == WHITE ? BTM_TB : WTM_TB;
    int16_t** CAPTURE_TBs = pos.side_to_move() == WHITE ? BPC_BTM_TBs : WPC_WTM_TBs;

    if (verbose) std::cout << pos << pos.fen() << std::endl;

    uint64_t ix = ix_from_pos(pos);
    int16_t tb_val = TB[ix];

    int16_t max_val = LOSS_IN(0);
    int16_t val;
    EGMoveList movelist = EGMoveList<FORWARD>(pos);
    for (Move move : movelist) {
        Piece capture = pos.do_move(move);
        uint64_t fwd_ix = ix_from_pos(pos);
        if (capture) {
            val = CAPTURE_TBs[type_of(capture)][fwd_ix];
            if (verbose) std::cout << "  " << move_to_uci(move) << "x " << val << " at ix: " << fwd_ix << std::endl;
        } else {
            val = MIRROR_TB[fwd_ix];
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

    uint64_t NPOS = num_positions();

    EGPosition pos;

    int16_t LEVEL = 0;

    uint64_t N_LEVEL_POS = 0;

    int16_t* LOSS_TB;
    int16_t* WIN_TB;
    int16_t** LOSS_TBs;
    uint64_t* LOSS_TBs_NPOS;
    int16_t** CAPTURE_TBs;
    Color LOSS_COLOR;

    TimePoint t0 = now();

    for (int wtm = 0; wtm <= 1; ++wtm) {
        if (wtm) {
            LOSS_TB = WTM_TB;
            LOSS_COLOR = WHITE;
        } else {
            LOSS_TB = BTM_TB;
            LOSS_COLOR = BLACK;
        }
        uint64_t N_UNUSED = 0;
        uint64_t N_CHECKMATE = 0;
        for (uint64_t ix = 0; ix < NPOS; ix++) {
            pos.reset();
            pos_at_ix(pos, ix, LOSS_COLOR, wpieces, bpieces);
            if (ix_from_pos(pos) != ix) {
                LOSS_TB[ix] = UNUSEDIX;
                N_UNUSED++;
                continue;
            }
            if (pos.is_legal_checkmate()) {
                LOSS_TB[ix] = LOSS_IN(0);
                N_CHECKMATE++;
                N_LEVEL_POS++;
            }
        }
        std::cout << "Checkmate count: " << N_CHECKMATE << std::endl;
        std::cout << N_UNUSED << " unused indices" << std::endl;
    }

    int16_t MIN_LEVEL = 0;
    for (int wtm = 0; wtm <= 1; ++wtm) {
        if (wtm) {
            LOSS_TBs = WPC_WTM_TBs;
            LOSS_TBs_NPOS = WPC_TBs_NPOS;
        } else {
            LOSS_TBs = BPC_BTM_TBs;
            LOSS_TBs_NPOS = BPC_TBs_NPOS;
        }
        for (PieceType capture_pt = NO_PIECE_TYPE; capture_pt <= QUEEN; ++capture_pt) {
            if (LOSS_TBs[capture_pt] == NULL) { continue; }
            for (uint64_t ix = 0; ix < LOSS_TBs_NPOS[capture_pt]; ix++) {
                int16_t val = LOSS_TBs[capture_pt][ix];
                if (0 < val && val <= WIN_IN(0)) {
                    MIN_LEVEL = std::max(MIN_LEVEL, (int16_t) (WIN_IN(0) - val));
                }
                if (0 > val && val >= LOSS_IN(0)) {
                    MIN_LEVEL = std::max(MIN_LEVEL, (int16_t) (val - LOSS_IN(0)));
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
                LOSS_TBs = WPC_WTM_TBs;
                LOSS_TBs_NPOS = WPC_TBs_NPOS;
                WIN_TB = BTM_TB;
            } else {
                LOSS_COLOR = BLACK;
                LOSS_TB = BTM_TB;
                LOSS_TBs = BPC_BTM_TBs;
                LOSS_TBs_NPOS = BPC_TBs_NPOS;
                WIN_TB = WTM_TB;
            }
            assert (LOSS_TB != WIN_TB);

            for (PieceType capture_pt = NO_PIECE_TYPE; capture_pt <= QUEEN; ++capture_pt) {
                if (LOSS_TBs[capture_pt] == NULL) { continue; }
                if (capture_pt) { 
                    if (LOSS_COLOR == WHITE) {
                        wpieces[capture_pt]--;
                    } else {
                        bpieces[capture_pt]--;
                    }
                }

                for (uint64_t ix = 0; ix < LOSS_TBs_NPOS[capture_pt]; ix++) {

                    // for all checkmate in LEVEL-1
                    if (LOSS_TBs[capture_pt][ix] == LOSS_IN(LEVEL-1)) {
                        pos.reset();
                        pos_at_ix(pos, ix, LOSS_COLOR, wpieces, bpieces);
                        // if (WIN_TB == WTM_TB && ix == 292) { std::cout << ix << " " << pos; }
                        for (Move move : EGMoveList<REVERSE>(pos, capture_pt)) {
                            pos.do_rev_move(move, capture_pt); // can be capture to transition to WIN_TB
                            uint64_t win_ix = ix_from_pos(pos);
                            // if (WIN_TB == WTM_TB && ix == 292) { std::cout << ix << " " << move_to_uci(move) << PieceToChar[capture_pt] << " -> " << win_ix << " with " << WIN_TB[win_ix] << std::endl; }
                            if (WIN_TB[win_ix] == 0 || WIN_TB[win_ix] == MAYBELOSS) { // MAYBELOSS could be set in ~wtm iteration, win is allowed to overwrite
                                WIN_TB[win_ix] = WIN_IN(LEVEL);

                                if (N_LEVEL_POS == 0) { std::cout << "WIN in " <<  LEVEL << ": " << pos.fen() << ", ix: " << win_ix << std::endl; }
                                N_LEVEL_POS++;

                                for (Move move2 : EGMoveList<REVERSE>(pos)) {
                                    pos.do_rev_move(move2); // no capture, stay in WIN_TB / LOSS_TB config

                                    uint64_t maybe_loss_ix = ix_from_pos(pos);
                                    if (LOSS_TB[maybe_loss_ix] == 0) {
                                        LOSS_TB[maybe_loss_ix] = MAYBELOSS;
                                    }

                                    pos.undo_rev_move(move2);
                                }
                            }

                            pos.undo_rev_move(move);
                        }
                    }
                }

                if (capture_pt) { 
                    if (LOSS_COLOR == WHITE) {
                        wpieces[capture_pt]++;
                    } else {
                        bpieces[capture_pt]++;
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
                CAPTURE_TBs = BPC_BTM_TBs;
                WIN_TB = BTM_TB;
            } else {
                LOSS_TB = BTM_TB;
                LOSS_COLOR = BLACK;
                CAPTURE_TBs = WPC_WTM_TBs;
                WIN_TB = WTM_TB;
            }

            for (uint64_t ix = 0; ix < NPOS; ix++) {
                if (LOSS_TB[ix] == MAYBELOSS) {
                // if (LOSS_TB[ix] == MAYBELOSS || LOSS_TB[ix] == 0) {
                    pos.reset();
                    pos_at_ix(pos, ix, LOSS_COLOR, wpieces, bpieces);
                    if (pos.sntm_in_check()) { continue; }

                    // check that all forward moves lead to checkmate in <= -(LEVEL-1)
                    EGMoveList moveList = EGMoveList<FORWARD>(pos);
                    int16_t max_val = LOSS_IN(0);
                    if (moveList.size() == 0) {
                        max_val = 0; // has to be stale mate
                        // assert(false); // cannot happen at MAYBELOSS position
                    } else {
                        for (Move move : moveList) {
                            Piece capture = pos.do_move(move);
                            if (capture) {
                                // max_val = std::max(max_val, (int16_t) 0);
                                uint64_t fwd_ix = ix_from_pos(pos);
                                int16_t val = -CAPTURE_TBs[type_of(capture)][fwd_ix];
                                max_val = std::max(max_val, val);
                            } else {
                                uint64_t fwd_ix = ix_from_pos(pos);
                                int16_t val = -WIN_TB[fwd_ix];
                                max_val = std::max(max_val, val);
                            }
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

                        assert(max_val == LOSS_IN(LEVEL-1));
                        LOSS_TB[ix] = LOSS_IN(LEVEL);
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
                if (pos.sntm_in_check()) { continue; }
                check_consistency(pos, false);
                N_LEVEL_POS++;
            }
            if (BTM_TB[ix] == LOSS_IN(LEVEL) || BTM_TB[ix] == WIN_IN(LEVEL)) {
                pos.reset();
                pos_at_ix(pos, ix, BLACK, wpieces, bpieces);
                if (pos.sntm_in_check()) { continue; }
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
            if (pos.sntm_in_check()) { continue; }
            check_consistency(pos, false);
        }
        if (BTM_TB[ix] == 0) {
            pos.reset();
            pos_at_ix(pos, ix, BLACK, wpieces, bpieces);
            if (pos.sntm_in_check()) { continue; }
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