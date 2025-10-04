
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

#include <unistd.h> // close
#include <fcntl.h> // open
#include <sys/mman.h> // mmap
#include <sys/stat.h> // file size

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



int16_t* load_egtb_mmap(int stm_pieces[6], int sntm_pieces[6]) {
    // uint64_t NPOS = compute_num_positions(stm_pieces, sntm_pieces);
    std::string filename = get_filename(stm_pieces, sntm_pieces);

    struct stat st;
    stat(filename.c_str(), &st);
    int fd = open(filename.c_str(), O_RDONLY);

    if (fd == -1) {
        printf("Could not open file %s.\n", filename.c_str());
        exit(EXIT_FAILURE);
    }
    int16_t* TB = (int16_t*) mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (TB == MAP_FAILED) {
        close(fd);
        perror("Error mmapping the file");
        exit(EXIT_FAILURE);
    }
    // std::cout << "mmap " << filename << " to " << TB << " with size " << st.st_size  << std::endl;
    close(fd);
    return TB;
}

void free_egtb_mmap(int16_t* TB, int stm_pieces[6], int sntm_pieces[6]) {
    std::string filename = get_filename(stm_pieces, sntm_pieces);
    struct stat st;
    stat(filename.c_str(), &st);
    // std::cout << "munmap " << filename << " from " << TB << " with size " << st.st_size << std::endl;
    int unmap = munmap(TB, st.st_size);
    if (unmap == -1) {
        perror("Error munmapping the file");
        exit(EXIT_FAILURE);
    }
}

int16_t* load_egtb_in_memory(int stm_pieces[6], int sntm_pieces[6]) {
    uint64_t NPOS = compute_num_positions(stm_pieces, sntm_pieces); 
    int16_t* TB = (int16_t*) calloc(sizeof(int16_t), NPOS);
    std::string filename = get_filename(stm_pieces, sntm_pieces);
    std::ifstream inputFileStream;
    inputFileStream.open(filename, std::ios::in|std::ios::binary);
    for(uint64_t i=0; i<NPOS; i++)
        inputFileStream.read((char*) &TB[i], sizeof(int16_t));
    return TB;
}
void free_egtb_in_memory(int16_t* TB) {
    free(TB);
}


int16_t* load_egtb(int stm_pieces[6], int sntm_pieces[6], bool mmap) {
    if (mmap)
        return load_egtb_mmap(stm_pieces, sntm_pieces);
    else
        return load_egtb_in_memory(stm_pieces, sntm_pieces);
}

void free_egtb(int16_t* TB, int stm_pieces[6], int sntm_pieces[6], bool mmap) {
    if (mmap)
        free_egtb_mmap(TB, stm_pieces, sntm_pieces);
    else
        free_egtb_in_memory(TB);
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
    uint64_t WTM_NPOS;
    uint64_t BTM_NPOS;

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
    
        WTM_NPOS = compute_num_positions(wpieces, bpieces);
        BTM_NPOS = compute_num_positions(bpieces, wpieces);
    }
    void allocate_and_load();

    ~GenEGTB() {
        free(WTM_TBs[NO_PIECE_TYPE][NO_PIECE_TYPE]);
        free(BTM_TBs[NO_PIECE_TYPE][NO_PIECE_TYPE]);
        WTM_TBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = NULL;
        BTM_TBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = NULL;

        for (PieceType promotion_pt = NO_PIECE_TYPE; promotion_pt <= QUEEN; ++promotion_pt) {
            for (PieceType capture_pt = NO_PIECE_TYPE; capture_pt <= QUEEN; ++capture_pt) {
                if (WTM_TBs[promotion_pt][capture_pt] != NULL) {
                    if (capture_pt) {
                        wpieces[capture_pt]--;
                    }
                    if (promotion_pt) {
                        bpieces[PAWN]--;
                        bpieces[promotion_pt]++;
                    }
                    free_egtb(WTM_TBs[promotion_pt][capture_pt], wpieces, bpieces, true);
                    WTM_TBs[promotion_pt][capture_pt] = NULL;
                    if (capture_pt) {
                        wpieces[capture_pt]++;
                    }
                    if (promotion_pt) {
                        bpieces[PAWN]++;
                        bpieces[promotion_pt]--;
                    }
                }
                if (BTM_TBs[promotion_pt][capture_pt] != NULL) {
                    if (capture_pt) {
                        bpieces[capture_pt]--;
                    }
                    if (promotion_pt) {
                        wpieces[PAWN]--;
                        wpieces[promotion_pt]++;
                    }
                    free_egtb(BTM_TBs[promotion_pt][capture_pt], wpieces, bpieces, true);
                    BTM_TBs[promotion_pt][capture_pt] = NULL;
                    if (capture_pt) {
                        bpieces[capture_pt]++;
                    }
                    if (promotion_pt) {
                        wpieces[PAWN]++;
                        wpieces[promotion_pt]--;
                    }
                }
            }
        }
    }

    void gen(int nthreads);
    void check_consistency(EGPosition &pos, bool verbose);
};

void GenEGTB::allocate_and_load() {
    this->WTM_TB = (int16_t*) calloc(sizeof(int16_t), WTM_NPOS);
    this->BTM_TB = (int16_t*) calloc(sizeof(int16_t), BTM_NPOS);


    std::cout << "White pieces: " << get_pieces_identifier(wpieces) << std::endl;
    std::cout << "Black pieces: " << get_pieces_identifier(bpieces) << std::endl;

    this->WTM_TBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = WTM_TB;
    this->BTM_TBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = BTM_TB;
    this->WTM_TBs_NPOS[NO_PIECE_TYPE][NO_PIECE_TYPE] = WTM_NPOS;
    this->BTM_TBs_NPOS[NO_PIECE_TYPE][NO_PIECE_TYPE] = BTM_NPOS;

    // captures
    for (PieceType capture_pt = PAWN; capture_pt <= QUEEN; ++capture_pt) {
        if (wpieces[capture_pt] > 0) {
            wpieces[capture_pt]--;
            uint64_t n = compute_num_positions(wpieces, bpieces);
            this->WTM_TBs_NPOS[NO_PIECE_TYPE][capture_pt] = n;
            this->WTM_TBs[NO_PIECE_TYPE][capture_pt] = load_egtb(wpieces, bpieces, true);
            std::cout << "Load " << get_egtb_identifier(wpieces, bpieces) << " for white " << PieceToChar[capture_pt] << " captured, white to move" << std::endl;
            wpieces[capture_pt]++;
        }
        if (bpieces[capture_pt] > 0) {
            bpieces[capture_pt]--;
            uint64_t n = compute_num_positions(wpieces, bpieces);
            this->BTM_TBs_NPOS[NO_PIECE_TYPE][capture_pt] = n;
            this->BTM_TBs[NO_PIECE_TYPE][capture_pt] = load_egtb(bpieces, wpieces, true); 
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
            this->WTM_TBs[promote_pt][NO_PIECE_TYPE] = load_egtb(wpieces, bpieces, true);
            std::cout << "Load " << get_egtb_identifier(wpieces, bpieces) << " for black promotion to " << PieceToChar[promote_pt] << ", white to move" << std::endl;
            bpieces[PAWN]++;
            bpieces[promote_pt]--;
        }
        if (wpieces[PAWN] > 0) {
            wpieces[PAWN]--;
            wpieces[promote_pt]++;
            uint64_t n = compute_num_positions(wpieces, bpieces);
            this->BTM_TBs_NPOS[promote_pt][NO_PIECE_TYPE] = n;
            this->BTM_TBs[promote_pt][NO_PIECE_TYPE] = load_egtb(bpieces, wpieces, true); 
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
                this->WTM_TBs[promote_pt][capture_pt] = load_egtb(wpieces, bpieces, true);
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
                this->BTM_TBs[promote_pt][capture_pt] = load_egtb(bpieces, wpieces, true); 
                std::cout << "Load " << get_egtb_identifier(bpieces, wpieces) << " for black " << PieceToChar[capture_pt] << " captured with white promotion to " << PieceToChar[promote_pt] << ", black to move" << std::endl;
                bpieces[capture_pt]++;
                wpieces[PAWN]++;
                wpieces[promote_pt]--;
            }
        }
    }
}


inline int16_t WIN_IN(int16_t level) { return 1000 - level; }
inline int16_t LOSS_IN(int16_t level) { return -1000 + level; }
// inline int16_t MAYBELOSS_IN(int16_t level) { return -1001; }
inline int16_t MAYBELOSS_IN(int16_t level) { return -11000 + level; }
// a position is MAYBELOSS_IN(level) if there is a move to position that is WIN_IN(level-1) and all other moves are <=WIN_IN(level-1) (draw and loss is possible)
#define UNUSED 1111
#define UNKNOWN 1110
inline int16_t IS_SET(int16_t val) { return LOSS_IN(0) <= val && val <= WIN_IN(0); }
// inline int16_t IS_UNSET(int16_t val) { return val == 0; }


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

void GenEGTB::gen(int nthreads) {
    std::cout << "Generate " << WTM_NPOS << " " << get_egtb_identifier(wpieces, bpieces) << " and " << BTM_NPOS << " " << get_egtb_identifier(bpieces, wpieces) << std::endl;

    if (egtb_exists(wpieces, bpieces)) {
        std::cout << "Already exists." << std::endl;
        return;
    }
    allocate_and_load();

    int16_t LEVEL = 0;

    uint64_t N_LEVEL_POS = 0;

    uint64_t LOSS_NPOS;
    int16_t* LOSS_TB;
    uint64_t WIN_NPOS;
    int16_t* WIN_TB;
    int16_t* (*LOSS_TBs)[6];
    uint64_t (*LOSS_TBs_NPOS)[6];
    int16_t* (*CAPTURE_TBs)[6];
    Color LOSS_COLOR;
        
    int16_t MIN_LEVEL = 0;

    TimePoint t0 = now();

    for (int wtm = 0; wtm <= 1; ++wtm) {
        if (wtm) {
            LOSS_NPOS = WTM_NPOS;
            LOSS_TB = WTM_TB;
            LOSS_COLOR = WHITE;
            CAPTURE_TBs = BTM_TBs;
        } else {
            LOSS_NPOS = BTM_NPOS;
            LOSS_TB = BTM_TB;
            LOSS_COLOR = BLACK;
            CAPTURE_TBs = WTM_TBs;
        }
        uint64_t N_UNUSED = 0;
        uint64_t N_SNTM_IN_CHECK = 0;
        uint64_t N_CHECKMATE = 0;

        #pragma omp parallel for num_threads(nthreads) schedule(static,64) reduction(+:N_LEVEL_POS) reduction(+:N_UNUSED) reduction(+:N_SNTM_IN_CHECK) reduction(+:N_CHECKMATE) reduction(max:MIN_LEVEL)
        for (uint64_t ix = 0; ix < LOSS_NPOS; ix++) {
            EGPosition pos;
            pos.reset();
            pos_at_ix(pos, ix, LOSS_COLOR, wpieces, bpieces);
            bool sntm_in_check = pos.sntm_in_check();
            if (ix_from_pos(pos) != ix || sntm_in_check) {
                LOSS_TB[ix] = UNUSED;
                N_UNUSED++;
                N_SNTM_IN_CHECK += sntm_in_check;
                continue;
            } else {
                LOSS_TB[ix] = UNKNOWN;
            }

            EGMoveList movelist = EGMoveList<FORWARD>(pos);
            if (movelist.size() == 0) {
                if (pos.stm_in_check()) {
                    LOSS_TB[ix] = LOSS_IN(0);
                    N_CHECKMATE++;
                    N_LEVEL_POS++;
                } else {
                    LOSS_TB[ix] = 0;
                }

            } else {
                
                int16_t max_val = LOSS_IN(0);
                bool has_full_eval = true;
                bool has_partial_eval = false;
                for (Move move : movelist) {
                    PieceType capture = pos.do_move(move);
                    PieceType promotion = move.type_of() == PROMOTION ? move.promotion_type() : NO_PIECE_TYPE;
                    if (!capture && !promotion) {
                        // move stays in TB, cannot determine eval
                        has_full_eval = false;
                    } else {
                        uint64_t fwd_ix = ix_from_pos(pos);
                        max_val = std::max(max_val, (int16_t) -CAPTURE_TBs[promotion][capture][fwd_ix]);
                        has_partial_eval = true;
                    }
                    pos.undo_move(move, capture);
                }

                // either has_full_eval or has_partial_eval since we have at least one move
                if (max_val > 0) {
                    max_val--;
                    MIN_LEVEL = std::max(MIN_LEVEL, (int16_t) (WIN_IN(0) - max_val));
                }
                if (max_val < 0) {
                    max_val++;
                    MIN_LEVEL = std::max(MIN_LEVEL, (int16_t) (max_val - LOSS_IN(0)));
                }

                if (has_full_eval) {
                    // all moves lead to dependent TB (cannot be stalemate since we have moves)
                    LOSS_TB[ix] = max_val;
                    N_LEVEL_POS++;
                }
                if (has_partial_eval) {
                    if (max_val < 0) {
                        // there is at least one move that leads to a winning position for sntm
                        // max_val is the upper bound on the val when only considering moves to dependent positions
                        LOSS_TB[ix] = MAYBELOSS_IN(max_val - LOSS_IN(0));
                    }
                    if (max_val >= 0) {
                        // known win and draw can be inferred from partial eval
                        LOSS_TB[ix] = max_val;
                    }
                }
            }
        }

        std::cout << "Stats for " << ((wtm) ? get_egtb_identifier(wpieces, bpieces) : get_egtb_identifier(bpieces, wpieces)) << ":\n";
        std::cout << "    Checkmate count: " << N_CHECKMATE << std::endl;
        std::cout << "    " << N_UNUSED << " unused indices (" << (double) N_UNUSED / LOSS_NPOS * 100 << "%)" << std::endl;
        std::cout << "    " << N_SNTM_IN_CHECK << " of which sntm in check (" << (double) N_SNTM_IN_CHECK / LOSS_NPOS * 100 << "%)" << std::endl;
    }
 
    std::cout << "MIN_LEVEL = " << int(MIN_LEVEL) << std::endl;

    while (true) {
        LEVEL++;
        N_LEVEL_POS = 0;

        for (int wtm = 0; wtm <= 1; ++wtm) {
            if (wtm) {
                LOSS_COLOR = WHITE;
                LOSS_NPOS = WTM_NPOS;
                LOSS_TB = WTM_TB;
                WIN_NPOS = BTM_NPOS;
                WIN_TB = BTM_TB;
            } else {
                LOSS_COLOR = BLACK;
                LOSS_NPOS = BTM_NPOS;
                LOSS_TB = BTM_TB;
                WIN_NPOS = WTM_NPOS;
                WIN_TB = WTM_TB;
            }
            assert (LOSS_TB != WIN_TB);

            #pragma omp parallel for num_threads(nthreads) schedule(static,64)
            for (uint64_t ix = 0; ix < LOSS_NPOS; ix++) {
                EGPosition pos;
                if (LOSS_TB[ix] == LOSS_IN(LEVEL-1)) {
                    pos.reset();
                    pos_at_ix(pos, ix, LOSS_COLOR, wpieces, bpieces);
                    assert (!pos.sntm_in_check());
                    for (Move move : EGMoveList<REVERSE>(pos)) {
                        pos.do_rev_move(move);
                        uint64_t win_ix = ix_from_pos(pos);
                        if (!IS_SET(WIN_TB[win_ix]) || WIN_TB[win_ix] < WIN_IN(LEVEL) ) {
                            WIN_TB[win_ix] = WIN_IN(LEVEL);
                        }
                        pos.undo_rev_move(move);
                    }
                }
            }

            #pragma omp parallel for num_threads(nthreads) schedule(static,64) reduction(+:N_LEVEL_POS)
            for (uint64_t win_ix = 0; win_ix < WIN_NPOS; win_ix++) {
                if (WIN_TB[win_ix] == WIN_IN(LEVEL)) {
                    EGPosition pos;
                    pos.reset();
                    pos_at_ix(pos, win_ix, ~LOSS_COLOR, wpieces, bpieces); // not much slower
                    if (pos.sntm_in_check()) {
                        std::cout << win_ix << " " << WIN_TB[win_ix] << pos;
                        assert (!pos.sntm_in_check());
                    }
                    N_LEVEL_POS++;

                    for (Move move : EGMoveList<REVERSE>(pos)) {
                        pos.do_rev_move(move); // no capture, stay in WIN_TB / LOSS_TB config
                        uint64_t maybe_loss_ix = ix_from_pos(pos);
                        if (!IS_SET(LOSS_TB[maybe_loss_ix])) {
                            if (LOSS_TB[maybe_loss_ix] == UNKNOWN) {
                                LOSS_TB[maybe_loss_ix] = MAYBELOSS_IN(LEVEL+1);
                            } else {
                                assert(LOSS_TB[maybe_loss_ix] - MAYBELOSS_IN(0) >= LEVEL+1);
                                // LOSS_TB[maybe_loss_ix] has to be MAYBELOSS_IN(SOME_LEVEL) where SOME_LEVEL >= LEVEL+1
                                // if SOME_LEVEL == LEVEL+1 this is what we would have set anyways
                                // if SOME_LEVEL > LEVEL+1 there has to be a move to dependent table which is < WIN_IN(LEVEL)
                                // SOME_LEVEL < LEVEL + 1 cannot happen as all such positions had to be considered previous iterations
                            }
                        }
                        pos.undo_rev_move(move);
                    }
                }
            }            
        }


        std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;

        LEVEL++;

        N_LEVEL_POS = 0;
        for (int wtm = 0; wtm <= 1; ++wtm) {
            if (wtm) {
                LOSS_NPOS = WTM_NPOS;
                LOSS_TB = WTM_TB;
                LOSS_COLOR = WHITE;
                CAPTURE_TBs = BTM_TBs;
                WIN_TB = BTM_TB;
            } else {
                LOSS_NPOS = BTM_NPOS;
                LOSS_TB = BTM_TB;
                LOSS_COLOR = BLACK;
                CAPTURE_TBs = WTM_TBs;
                WIN_TB = WTM_TB;
            }
            assert (CAPTURE_TBs[NO_PIECE_TYPE][NO_PIECE_TYPE] == WIN_TB);

            #pragma omp parallel for num_threads(nthreads) schedule(static,64) reduction(+:N_LEVEL_POS)
            for (uint64_t ix = 0; ix < LOSS_NPOS; ix++) {
                EGPosition pos;
                if (LOSS_TB[ix] == MAYBELOSS_IN(LEVEL)) {
                    pos.reset();
                    pos_at_ix(pos, ix, LOSS_COLOR, wpieces, bpieces);
                    assert (!pos.sntm_in_check()); // should be UNUSED_IX if sntm_in_check

                    // check that all forward moves lead to checkmate in <= -(LEVEL-1)
                    EGMoveList moveList = EGMoveList<FORWARD>(pos);
                    int16_t max_val = LOSS_IN(LEVEL-1); // there is at least one move that leads to WIN_IN(LEVEL-1)
                    if (moveList.size() == 0) {
                        max_val = 0; // has to be stale mate
                        if (LOSS_TB[ix] == MAYBELOSS_IN(LEVEL))
                            std::cout << pos << ix << std::endl;
                        assert(LOSS_TB[ix] != MAYBELOSS_IN(LEVEL)); // cannot happen at MAYBELOSS position
                    } else {
                        for (Move move : moveList) {
                            PieceType capture = pos.do_move(move);
                            PieceType promotion = move.type_of() == PROMOTION ? move.promotion_type() : NO_PIECE_TYPE;

                            if (!promotion && !capture) {
                                uint64_t fwd_ix = ix_from_pos(pos);
                                int16_t val = WIN_TB[fwd_ix];
                                if (val == UNKNOWN) {
                                    max_val = 0;
                                } else {
                                    max_val = std::max(max_val, (int16_t) -val);
                                }
                            }

                            pos.undo_move(move, capture);
                            
                            if (max_val > LOSS_IN(LEVEL-1)) { break; }
                        }
                    }
                    
                    if (max_val == LOSS_IN(LEVEL-1)) {
                        LOSS_TB[ix] = LOSS_IN(LEVEL);
                        N_LEVEL_POS++;
                    } else {
                        // maybeloss can be refuted by finding non-capture non-promo move
                        LOSS_TB[ix] = UNKNOWN;
                    }
                    
                }
            }
        }

        std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;

        if (LEVEL > MIN_LEVEL && N_LEVEL_POS == 0) { break; }
    }


    uint64_t MAX_NPOS = std::max(WTM_NPOS, BTM_NPOS);
    #pragma omp parallel for num_threads(nthreads) schedule(static,64)
    for (uint64_t ix = 0; ix < MAX_NPOS; ix++) {
        // all entries that are not known wins or losses are draws (TODO: set UNUSED to 0 as well in the future)
        if (ix < WTM_NPOS && WTM_TB[ix] == UNKNOWN) WTM_TB[ix] = 0;
        if (ix < BTM_NPOS && BTM_TB[ix] == UNKNOWN) BTM_TB[ix] = 0;
    }

    TimePoint t1 = now();
    std::cout << "Finished in " << (double) (t1 - t0) / 1000.0 << "s." << std::endl;

    // Consistency checks:

    LEVEL = 0;
    N_LEVEL_POS = 0;
    while (true) {
        #pragma omp parallel for num_threads(nthreads) schedule(static,64)
        for (uint64_t ix = 0; ix < MAX_NPOS; ix++) {
            EGPosition pos;
            if ((ix < WTM_NPOS) && (WTM_TB[ix] == LOSS_IN(LEVEL) || WTM_TB[ix] == WIN_IN(LEVEL))) {
                pos.reset();
                pos_at_ix(pos, ix, WHITE, wpieces, bpieces);
                assert (!pos.sntm_in_check());
                check_consistency(pos, false);
                N_LEVEL_POS++;
            }
            if ((ix < BTM_NPOS) && (BTM_TB[ix] == LOSS_IN(LEVEL) || BTM_TB[ix] == WIN_IN(LEVEL))) {
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

    #pragma omp parallel for num_threads(nthreads) schedule(static,64)
    for (uint64_t ix = 0; ix < MAX_NPOS; ix++) {
        EGPosition pos;
        if (ix < WTM_NPOS && WTM_TB[ix] == 0) {
            pos.reset();
            pos_at_ix(pos, ix, WHITE, wpieces, bpieces);
            assert(!pos.sntm_in_check());
            check_consistency(pos, false);
        }
        if (ix < BTM_NPOS && BTM_TB[ix] == 0) {
            pos.reset();
            pos_at_ix(pos, ix, BLACK, wpieces, bpieces);
            assert(!pos.sntm_in_check());
            check_consistency(pos, false);
        }
    }
    std::cout << "Consistency check passed for draws" << std::endl;


    for (int wtm = 0; wtm <= 1; ++wtm) {
        int16_t* _TB = wtm ? WTM_TB : BTM_TB;
        uint64_t _NPOS = wtm ? WTM_NPOS : BTM_NPOS;
        std::string egtb = wtm ? get_egtb_identifier(wpieces, bpieces) : get_egtb_identifier(bpieces, wpieces);

        uint64_t wins = 0;
        uint64_t draws = 0;
        uint64_t losses = 0;
        #pragma omp parallel for num_threads(nthreads) schedule(static,64) reduction(+:wins) reduction(+:draws) reduction(+:losses)
        for (uint64_t ix = 0; ix < _NPOS; ix++) {
            if (_TB[ix] == UNUSED) { continue; }
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
    std::cout << "Stored " << get_egtb_identifier(wpieces, bpieces) << " and " << get_egtb_identifier(bpieces, wpieces) << std::endl;
}

#endif