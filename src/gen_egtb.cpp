
#ifndef GEN_EGTB_H_INCLUDED
#define GEN_EGTB_H_INCLUDED

#include "kkx.h"
#include "linearize.h"
#include "eg_position.h"
#include "eg_movegen.h"
#include "triangular_indexes.h"
#include "uci.h"
#include "misc.h"
#include "egtb.h"
#include <string>
#include "values.h"

#define CHUNKSIZE 2048

class GenEGTB {
    std::string folder;
    int wpieces[6];
    int bpieces[6];
    int n_pieces;

    EGTB* WTM_EGTB;
    EGTB* BTM_EGTB;

    EGTB* WTM_EGTBs[6][6];
    EGTB* BTM_EGTBs[6][6];

    bool do_consistency_checks;
    bool disable_allocate_promotion_tb;
    bool compress;

    uint64_t bytes_allocated;
    uint64_t bytes_mmaped;

    uint64_t CONSISTENCY_CHECK_MAX_BYTES;
    uint64_t COMPRESSION_LEVEL;
    uint64_t BLOCKSIZE;

public:
    GenEGTB(
        int wpieces_[6], int bpieces_[6], std::string folder_,
        bool do_consistency_checks_, bool disable_allocate_promotion_tb_, bool compress_,
        uint64_t CONSISTENCY_CHECK_MAX_BYTES_, uint64_t COMPRESSION_LEVEL_, uint64_t BLOCKSIZE_) {
        
        // std::cout << "GenEGTB\n";
        this->folder = folder_;
        this->n_pieces = 0;
        for (int i = 0; i < 6; i++) {
            this->wpieces[i] = wpieces_[i];
            this->bpieces[i] = bpieces_[i];
            this->n_pieces += wpieces_[i] + bpieces_[i];
        }

        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < 6; j++) {
                this->WTM_EGTBs[i][j] = NULL;
                this->BTM_EGTBs[i][j] = NULL;
            }
        }

        this->WTM_EGTB = new EGTB(this->wpieces, this->bpieces);
        this->BTM_EGTB = new EGTB(this->bpieces, this->wpieces);

        std::cout << "White pieces: " << get_pieces_identifier(wpieces) << std::endl;
        std::cout << "Black pieces: " << get_pieces_identifier(bpieces) << std::endl;
        uint64_t working_bytes = (this->WTM_EGTB->num_pos * 2) + (this->BTM_EGTB->num_pos * 2);
        std::cout << "working tb: " << working_bytes / (1024*1024) << " MiB"  << std::endl;

        this->WTM_EGTBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = WTM_EGTB;
        this->BTM_EGTBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = BTM_EGTB;

        // captures
        uint64_t capture_bytes = 0;
        for (PieceType capture_pt = PAWN; capture_pt <= QUEEN; ++capture_pt) {
            if (wpieces[capture_pt] > 0) {
                wpieces[capture_pt]--;
                EGTB* egtb = new EGTB(wpieces, bpieces);
                this->WTM_EGTBs[NO_PIECE_TYPE][capture_pt] = egtb;
                std::cout << " " << egtb->id << " for white " << PieceToChar[capture_pt] << " captured, white to move" << std::endl;
                capture_bytes += egtb->num_pos * 2;
                wpieces[capture_pt]++;
            }
            if (bpieces[capture_pt] > 0) {
                bpieces[capture_pt]--;
                EGTB* egtb = new EGTB(bpieces,wpieces);
                this->BTM_EGTBs[NO_PIECE_TYPE][capture_pt] = egtb;
                std::cout << " " << egtb->id << " for black " << PieceToChar[capture_pt] << " captured, black to move"  << std::endl;
                capture_bytes += egtb->num_pos * 2;
                bpieces[capture_pt]++;
            }
        }
        std::cout << "capture tbs: " << capture_bytes / (1024*1024) << " MiB (total: " << (working_bytes + capture_bytes) / (1024*1024) << " MiB)" << std::endl;

        // capture promotions
        uint64_t capture_promo_bytes = 0;
        for (PieceType promote_pt = KNIGHT; promote_pt <= QUEEN; ++promote_pt) {
            for (PieceType capture_pt = KNIGHT; capture_pt <= QUEEN; ++capture_pt) {
                if (bpieces[PAWN] > 0 && wpieces[capture_pt] > 0) {
                    wpieces[capture_pt]--;
                    bpieces[PAWN]--;
                    bpieces[promote_pt]++;
                    EGTB* egtb = new EGTB(wpieces, bpieces);
                    this->WTM_EGTBs[promote_pt][capture_pt] = egtb;
                    std::cout << " " << egtb->id << " for white " << PieceToChar[capture_pt] << " captured with black promotion to " << PieceToChar[promote_pt] << ", white to move" << std::endl;
                    capture_promo_bytes += egtb->num_pos * 2;
                    wpieces[capture_pt]++;
                    bpieces[PAWN]++;
                    bpieces[promote_pt]--;
                }
                if (wpieces[PAWN] > 0 && bpieces[capture_pt] > 0) {
                    bpieces[capture_pt]--;
                    wpieces[PAWN]--;
                    wpieces[promote_pt]++;
                    EGTB* egtb = new EGTB(bpieces,wpieces);
                    this->BTM_EGTBs[promote_pt][capture_pt] = egtb;
                    std::cout << " " << egtb->id << " for black " << PieceToChar[capture_pt] << " captured with white promotion to " << PieceToChar[promote_pt] << ", black to move" << std::endl;
                    capture_promo_bytes += egtb->num_pos * 2;
                    bpieces[capture_pt]++;
                    wpieces[PAWN]++;
                    wpieces[promote_pt]--;
                }
            }
        }
        std::cout << "capture-promotion tbs: " << capture_promo_bytes / (1024*1024) << " MiB (total: " << (working_bytes + capture_bytes + capture_promo_bytes) / (1024*1024) << " MiB)" << std::endl;

        // promotions
        uint64_t promotion_bytes = 0;
        for (PieceType promote_pt = KNIGHT; promote_pt <= QUEEN; ++promote_pt) {
            if (bpieces[PAWN] > 0) {
                bpieces[PAWN]--;
                bpieces[promote_pt]++;
                EGTB* egtb = new EGTB(wpieces, bpieces);
                this->WTM_EGTBs[promote_pt][NO_PIECE_TYPE] = egtb;
                std::cout << " " << egtb->id << " for black promotion to " << PieceToChar[promote_pt] << ", white to move" << std::endl;
                promotion_bytes += egtb->num_pos * 2;
                bpieces[PAWN]++;
                bpieces[promote_pt]--;
            }
            if (wpieces[PAWN] > 0) {
                wpieces[PAWN]--;
                wpieces[promote_pt]++;
                EGTB* egtb = new EGTB(bpieces, wpieces); 
                this->BTM_EGTBs[promote_pt][NO_PIECE_TYPE] = egtb;
                std::cout << " " << egtb->id << " for white promotion to " << PieceToChar[promote_pt] << ", black to move" << std::endl;
                promotion_bytes += egtb->num_pos * 2;
                wpieces[PAWN]++;
                wpieces[promote_pt]--;
            }
        }
        std::cout << "promotion tbs: " << promotion_bytes / (1024*1024) << " MiB (total: " << (working_bytes + capture_bytes + capture_promo_bytes + promotion_bytes) / (1024*1024) << " MiB)" <<  std::endl;

        this->do_consistency_checks = do_consistency_checks_;
        this->disable_allocate_promotion_tb = disable_allocate_promotion_tb_;
        this->compress = compress_;

        this->CONSISTENCY_CHECK_MAX_BYTES = CONSISTENCY_CHECK_MAX_BYTES_;
        this->COMPRESSION_LEVEL = COMPRESSION_LEVEL_;
        this->BLOCKSIZE = BLOCKSIZE_;

        this->bytes_allocated = 0;
        this->bytes_mmaped = 0;
    }

    ~GenEGTB() {
        assert(!WTM_EGTB->loaded);
        delete WTM_EGTB;
        this->WTM_EGTB = NULL;

        assert(!BTM_EGTB->loaded);
        delete BTM_EGTB;
        this->BTM_EGTB = NULL;

        this->WTM_EGTBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = NULL;
        this->BTM_EGTBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = NULL;

        for (PieceType promote_pt = NO_PIECE_TYPE; promote_pt <= QUEEN; ++promote_pt) {
            for (PieceType capture_pt = NO_PIECE_TYPE; capture_pt <= QUEEN; ++capture_pt) {
                if (WTM_EGTBs[promote_pt][capture_pt] != NULL) {
                    assert (!WTM_EGTBs[promote_pt][capture_pt]->loaded);
                    delete WTM_EGTBs[promote_pt][capture_pt];
                }
                if (BTM_EGTBs[promote_pt][capture_pt] != NULL) {
                    assert(!BTM_EGTBs[promote_pt][capture_pt]->loaded);
                    delete BTM_EGTBs[promote_pt][capture_pt];
                    BTM_EGTBs[promote_pt][capture_pt] = NULL;
                }
            }
        }
    }

    void add_to_bytes_count(EGTB* egtb) {
        bytes_allocated += (egtb->num_pos * 2) * !egtb->mmaped;
        bytes_mmaped += (egtb->num_pos * 2) * egtb->mmaped;
    }

    void sub_from_bytes_count(EGTB* egtb) {
        bytes_allocated -= (egtb->num_pos * 2) * !egtb->mmaped;
        bytes_mmaped -= (egtb->num_pos * 2) * egtb->mmaped;
    }

    void allocate();
    void load_tb_dependencies(int nthreads, bool allocate_promotion_tb, bool verbose_when_disabled);
    void deallocate();
    void free_tb_dependencies();

    void retrograde_promotion_tbs(int nthreads);

    void gen(int nthreads);
    void check_consistency(EGPosition &pos, bool verbose);
    void check_consistency_allocated_by_level(int nthreads, int16_t MAX_LEVEL);
    void check_consistency_allocated(int nthreads);
    void check_consistency_max_bytes_allocated(int nthreads, uint64_t max_bytes);

};

void GenEGTB::allocate() {
    assert (!WTM_EGTB->loaded);
    this->WTM_EGTB->TB = (int16_t*) calloc(WTM_EGTB->num_pos, sizeof(int16_t));
    this->WTM_EGTB->mmaped = false;
    this->WTM_EGTB->loaded = true;
    assert (!BTM_EGTB->loaded);
    this->BTM_EGTB->TB = (int16_t*) calloc(BTM_EGTB->num_pos, sizeof(int16_t));
    this->BTM_EGTB->mmaped = false;
    this->BTM_EGTB->loaded = true;

    add_to_bytes_count(this->WTM_EGTB);
    add_to_bytes_count(this->BTM_EGTB);
}

// loads in memory / no mmap
void GenEGTB::load_tb_dependencies(int nthreads, bool allocate_promotion_tb, bool verbose_when_disabled) {
    EGTB* egtb;
    for (PieceType promote_pt = NO_PIECE_TYPE; promote_pt <= QUEEN; ++promote_pt) {
        for (PieceType capture_pt = NO_PIECE_TYPE; capture_pt <= QUEEN; ++capture_pt) {
            if (promote_pt == NO_PIECE_TYPE && capture_pt == NO_PIECE_TYPE) continue;

            egtb = this->WTM_EGTBs[promote_pt][capture_pt];
            if (egtb != NULL) {
                if (promote_pt && !capture_pt && !allocate_promotion_tb) {
                    if (verbose_when_disabled) std::cout << "Disabled loading " << egtb->id << " for black promotion to " << PieceToChar[promote_pt] << ", white to move" << std::endl;
                } else {
                    if (egtb->loaded) {
                        std::cout << "Already loaded ";
                    } else {
                        egtb->maybe_decompress_and_load_egtb(folder, nthreads);
                        add_to_bytes_count(egtb);
                        std::cout << "Loaded ";
                    }
                    if (!promote_pt && capture_pt) std::cout << egtb->id << " for white " << PieceToChar[capture_pt] << " captured, white to move" << std::endl;
                    if (promote_pt && capture_pt)  std::cout << egtb->id << " for white " << PieceToChar[capture_pt] << " captured with black promotion to " << PieceToChar[promote_pt] << ", white to move" << std::endl;
                    if (promote_pt && !capture_pt) std::cout << egtb->id << " for black promotion to " << PieceToChar[promote_pt] << ", white to move" << std::endl;
                }
            }

            egtb = this->BTM_EGTBs[promote_pt][capture_pt];
            if (egtb != NULL) {
                if (promote_pt && !capture_pt && !allocate_promotion_tb) {
                    if (verbose_when_disabled) std::cout << "Disabled loading " << egtb->id << " for white promotion to " << PieceToChar[promote_pt] << ", black to move" << std::endl;
                } else {
                    if (egtb->loaded) {
                        std::cout << "Already loaded ";
                    } else {
                        egtb->maybe_decompress_and_load_egtb(folder, nthreads);
                        add_to_bytes_count(egtb);
                        std::cout << "Loaded ";
                    }
                    if (!promote_pt && capture_pt) std::cout << egtb->id << " for black " << PieceToChar[capture_pt] << " captured, black to move"  << std::endl;
                    if (promote_pt && capture_pt)  std::cout << egtb->id << " for black " << PieceToChar[capture_pt] << " captured with white promotion to " << PieceToChar[promote_pt] << ", black to move" << std::endl;
                    if (promote_pt && !capture_pt) std::cout << egtb->id << " for white promotion to " << PieceToChar[promote_pt] << ", black to move" << std::endl;
                }
            }
        }
    }
}

void GenEGTB::deallocate() {

    if (WTM_EGTB->loaded) {
        sub_from_bytes_count(this->WTM_EGTB);
        WTM_EGTB->free_tb();
    }
    if (BTM_EGTB->loaded) {
        sub_from_bytes_count(this->BTM_EGTB);
        BTM_EGTB->free_tb();
    }

}

void GenEGTB::free_tb_dependencies() {
    EGTB* egtb;
    for (PieceType promote_pt = NO_PIECE_TYPE; promote_pt <= QUEEN; ++promote_pt) {
        for (PieceType capture_pt = NO_PIECE_TYPE; capture_pt <= QUEEN; ++capture_pt) {
            egtb = WTM_EGTBs[promote_pt][capture_pt];
            if (egtb != NULL) {
                if (egtb->loaded) {
                    sub_from_bytes_count(egtb);
                    egtb->free_tb();
                }
            }
            egtb = BTM_EGTBs[promote_pt][capture_pt];
            if (egtb != NULL) {
                if (egtb->loaded) {
                    sub_from_bytes_count(egtb);
                    egtb->free_tb();
                }
            }
        }
    }
}


void GenEGTB::check_consistency(EGPosition &pos, bool verbose) {
    assert (!pos.sntm_in_check());
    EGTB* egtb = pos.side_to_move() == WHITE ? WTM_EGTB : BTM_EGTB;
    EGTB* (*CAPTURE_TBs)[6] = pos.side_to_move() == WHITE ? BTM_EGTBs : WTM_EGTBs;

    if (verbose) std::cout << pos << pos.fen() << std::endl;

    uint64_t ix = egtb->ix_from_pos(pos);
    int16_t tb_val = egtb->TB[ix];

    int16_t max_val = LOSS_IN(0);
    int16_t val;
    EGMoveList movelist = EGMoveList(pos);
    for (Move move : movelist) {
        UndoInfo u = pos.do_move(move);
        PieceType promotion = move.type_of() == PROMOTION ? move.promotion_type() : NO_PIECE_TYPE;
        EGTB* cap_egtb = CAPTURE_TBs[promotion][u.captured];
        uint64_t fwd_ix = cap_egtb->ix_from_pos(pos);
        val = cap_egtb->TB[fwd_ix];
        if (u.captured) {
            if (verbose) std::cout << "  " << move_to_uci(move) << "x " << val << " at ix: " << fwd_ix << std::endl;
        } else {
            if (verbose) std::cout << "  " << move_to_uci(move) << " " << val << " at ix: " << fwd_ix << std::endl;
        }
        max_val = std::max(max_val, (int16_t) -val);

        pos.undo_move(u);
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
        if (max_val > 0) max_val--;
        if (max_val < 0) max_val++;
    }

    if (max_val != tb_val) {
        std::cout << "INCONSISTENCY: at ix " << ix << " max_val: " << max_val << " vs tb_val:" << tb_val << std::endl;
        if (!verbose)
            check_consistency(pos, true);
        exit(1);
    }
}

void GenEGTB::check_consistency_allocated(int nthreads) {
    EGTB* LOSS_EGTB;
    EGTB* (*CAPTURE_EGTBs)[6];
    Color LOSS_COLOR;

    for (int wtm = 0; wtm <= 1; ++wtm) {
        if (wtm) {
            LOSS_EGTB = WTM_EGTB;
            LOSS_COLOR = WHITE;
            CAPTURE_EGTBs = BTM_EGTBs;
        } else {
            LOSS_EGTB = BTM_EGTB;
            LOSS_COLOR = BLACK;
            CAPTURE_EGTBs = WTM_EGTBs;
        }

        #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE)
        for (uint64_t ix = 0; ix < LOSS_EGTB->num_pos; ix++) {
            EGPosition pos;
            pos.reset();
            LOSS_EGTB->pos_at_ix(pos, ix, LOSS_COLOR);

            int16_t tb_val = LOSS_EGTB->TB[ix];
            if (!LOSS_EGTB->pos_ix_is_used(pos, ix)) {
                assert (tb_val == 0); // || tb_val == UNUSED
                continue;
            }

            EGMoveList movelist = EGMoveList(pos);
            if (movelist.size() == 0) {
                if (pos.stm_in_check()) {
                    assert(tb_val == LOSS_IN(0));
                } else {
                    assert(tb_val == 0);
                }

            } else {
                
                int16_t max_val = LOSS_IN(0);
                for (Move move : movelist) {
                    UndoInfo u = pos.do_move(move);
                    PieceType promotion = move.type_of() == PROMOTION ? move.promotion_type() : NO_PIECE_TYPE;
                    EGTB* cap_egtb = CAPTURE_EGTBs[promotion][u.captured];
                    uint64_t fwd_ix = cap_egtb->ix_from_pos(pos);
                    max_val = std::max(max_val, (int16_t) -cap_egtb->TB[fwd_ix]);
                    pos.undo_move(u);
                }

                if (max_val > 0) max_val--;
                if (max_val < 0) max_val++;

                assert(tb_val == max_val);
            }
        }
    }
}


void GenEGTB::check_consistency_max_bytes_allocated(int nthreads, uint64_t max_bytes) {
    assert(!WTM_EGTB->loaded);
    assert(!BTM_EGTB->loaded);
    for (PieceType promote_pt = KNIGHT; promote_pt <= QUEEN; ++promote_pt) {
        if (WTM_EGTBs[promote_pt][NO_PIECE_TYPE] != NULL) assert (!WTM_EGTBs[promote_pt][NO_PIECE_TYPE]->loaded);
        if (BTM_EGTBs[promote_pt][NO_PIECE_TYPE] != NULL) assert (!BTM_EGTBs[promote_pt][NO_PIECE_TYPE]->loaded);
    }
    std::cout << "Loading egtbs for consistency checks with " << (max_bytes / (1024*1024)) << " MiB allocation budget." <<  std::endl;
    load_tb_dependencies(nthreads, false, false); // load all capture and capture-promotion tables (if not already loaded)

    EGTB* LOSS_EGTB;
    EGTB* (*CAPTURE_EGTBs)[6];
    Color LOSS_COLOR;

    for (int wtm = 0; wtm <= 1; ++wtm) {
        if (wtm) {
            LOSS_EGTB = WTM_EGTB;
            LOSS_COLOR = WHITE;
            CAPTURE_EGTBs = BTM_EGTBs;
        } else {
            LOSS_EGTB = BTM_EGTB;
            LOSS_COLOR = BLACK;
            CAPTURE_EGTBs = WTM_EGTBs;
        }

        // query from compressed file
        LOSS_EGTB->init_compressed_tb(folder);
        assert (LOSS_EGTB->CTB->block_size == BLOCKSIZE);

        //                 NP, P, N, B, R, Q
        bool checked[6] = { 0, 1, 0, 0, 0, 0};

        uint64_t n_max_obtained = (LOSS_EGTB->num_pos / 8) + (LOSS_EGTB->num_pos % 8 != 0);
        uint8_t* max_obtained = (uint8_t*) calloc(n_max_obtained, sizeof(uint8_t));
        if (LOSS_EGTB->num_pos % 8 != 0) {
            // set all unused bits to 1
            for (uint64_t ix = LOSS_EGTB->num_pos; ix < n_max_obtained * 8; ix++) {
                uint64_t ix_m = ix / 8;
                uint64_t ix_of = ix % 8;
                max_obtained[ix_m] |= (1 << ix_of);
            }
        }
        
        while (true) {
            bool all_checked = true;
            bool loaded_one_egtb = false;
            for (PieceType promote_pt = NO_PIECE_TYPE; promote_pt <= QUEEN; ++promote_pt) {
                EGTB* cap_egtb = CAPTURE_EGTBs[promote_pt][NO_PIECE_TYPE];
                if (cap_egtb != NULL && !checked[promote_pt]) {
                    all_checked = false;
                    if (bytes_allocated + cap_egtb->num_pos*2  > max_bytes) {
                        break;
                    }
                    std::cout << "Loading " << cap_egtb->id << " with " << cap_egtb->num_pos*2 / (1024*1024)  << " MiB to check consistency for " << LOSS_EGTB->id << "... ";
                    cap_egtb->maybe_decompress_and_load_egtb(folder, nthreads);
                    add_to_bytes_count(cap_egtb);
                    loaded_one_egtb = true;
                    std::cout << "(total allocated " << bytes_allocated / (1024*1024) << " MiB)" << std::endl;
                }
            }
            if (all_checked) break;
            if (!loaded_one_egtb) {
                std::cout << "Could not load single EGTB to check consistency. (total allocated: " << bytes_allocated / (1024*1204) << " MiB)" << std::endl;
                exit(1); 
            }

            // BLOCKSIZE is divisible by 8 thus no race conditions on ix_m
            assert (BLOCKSIZE % 8 == 0);
            #pragma omp parallel num_threads(nthreads) 
            {

                DecompressCtx* dctx = new DecompressCtx(BLOCKSIZE);

                #pragma omp for schedule(static,BLOCKSIZE)
                for (uint64_t ix = 0; ix < LOSS_EGTB->num_pos; ix++) {
                    EGPosition pos;
                    pos.reset();
                    LOSS_EGTB->pos_at_ix(pos, ix, LOSS_COLOR);

                    int16_t tb_val = LOSS_EGTB->get_value_dctx(ix, dctx);

                    uint64_t ix_m = ix / 8;
                    uint64_t ix_of = ix % 8;


                    if (!LOSS_EGTB->pos_ix_is_used(pos, ix)) {
                        assert (tb_val == 0); // || tb_val == UNUSED
                        max_obtained[ix_m] |= (1 << ix_of);
                        continue;
                    }
                    
                    EGMoveList movelist = EGMoveList(pos);
                    if (movelist.size() == 0) {
                        if (pos.stm_in_check()) {
                            assert(tb_val == LOSS_IN(0));
                        } else {
                            assert(tb_val == 0);
                        }
                        max_obtained[ix_m] |= (1 << ix_of);
                    } else {
                        
                        int16_t max_val = LOSS_IN(0);
                        for (Move move : movelist) {
                            UndoInfo u = pos.do_move(move);
                            PieceType promotion = move.type_of() == PROMOTION ? move.promotion_type() : NO_PIECE_TYPE;
                            EGTB* cap_egtb = CAPTURE_EGTBs[promotion][u.captured];
                            if (cap_egtb->loaded) {
                                uint64_t fwd_ix = cap_egtb->ix_from_pos(pos);
                                max_val = std::max(max_val, (int16_t) -cap_egtb->TB[fwd_ix]);
                            }
                            pos.undo_move(u);
                        }

                        if (max_val > 0) max_val--;
                        if (max_val < 0) max_val++;

                        assert(max_val <= tb_val);
                        if (max_val == tb_val) {
                            max_obtained[ix_m] |= (1 << ix_of);
                        }
                    }
                }

                #pragma omp critical
                {
                    delete dctx;
                }
            }

            std::cout << "Checked consistency for " << LOSS_EGTB->id << ": ";
            for (PieceType promote_pt = NO_PIECE_TYPE; promote_pt <= QUEEN; ++promote_pt) {
                EGTB* cap_egtb = CAPTURE_EGTBs[promote_pt][NO_PIECE_TYPE];
                if (cap_egtb != NULL && cap_egtb->loaded) {
                    sub_from_bytes_count(cap_egtb);
                    cap_egtb->free_tb();
                    checked[promote_pt] = true;
                    std::cout << cap_egtb->id << " ";
                }
            }
            std::cout << std::endl;
        }

        #pragma omp parallel for num_threads(nthreads) schedule(static,2048)
        for (uint64_t ix_m = 0; ix_m < n_max_obtained; ix_m++) {
            if (max_obtained[ix_m] != 0xff) {
                std::cout << ix_m << ": " << int(max_obtained[ix_m]) << std::endl;
                for (uint64_t ix_of = 0; ix_of < 8; ix_of++) {
                    if ((max_obtained[ix_m] & (1 << ix_of)) == 0) {
                        uint64_t ix = ix_m * 8 + ix_of;
                        if (ix < LOSS_EGTB->num_pos) {
                            EGPosition pos;
                            pos.reset();
                            LOSS_EGTB->pos_at_ix(pos, ix, LOSS_COLOR);
                            std::cout << "INCONSISTENCY: Did not obtain max " << LOSS_EGTB->TB[ix] <<  " at " << ix << ":\n" << pos << pos.fen() << std::endl;
                            for (Move move: EGMoveList(pos)) std::cout << move_to_uci(move) << " ";
                            std::cout << std::endl;
                        }
                    }
                }
            }
            assert (max_obtained[ix_m] == 0xff);
        }
        free(max_obtained);

        LOSS_EGTB->free_compressed_tb();
    }
}

void GenEGTB::check_consistency_allocated_by_level(int nthreads, int16_t MAX_LEVEL) {
    for (int16_t LEVEL = 0; LEVEL <= MAX_LEVEL; LEVEL++) {
        #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE)
        for (uint64_t ix = 0; ix < WTM_EGTB->num_pos; ix++) {
            EGPosition pos;
            if ((WTM_EGTB->TB[ix] == LOSS_IN(LEVEL) || WTM_EGTB->TB[ix] == WIN_IN(LEVEL))) {
                pos.reset();
                WTM_EGTB->pos_at_ix(pos, ix, WHITE);
                assert (WTM_EGTB->pos_ix_is_used(pos,ix));
                check_consistency(pos, false);
            }
        }
        std::cout << "WTM Consistency check passed for level " << LEVEL << std::endl;

        #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE)
        for (uint64_t ix = 0; ix < BTM_EGTB->num_pos; ix++) {
            EGPosition pos;
            if ((BTM_EGTB->TB[ix] == LOSS_IN(LEVEL) || BTM_EGTB->TB[ix] == WIN_IN(LEVEL))) {
                pos.reset();
                BTM_EGTB->pos_at_ix(pos, ix, BLACK);
                assert (BTM_EGTB->pos_ix_is_used(pos,ix));
                check_consistency(pos, false);
            }
        }
        std::cout << "BTM Consistency check passed for level " << LEVEL << std::endl;
    }

    #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE)
    for (uint64_t ix = 0; ix < WTM_EGTB->num_pos; ix++) {
        EGPosition pos;
        if (WTM_EGTB->TB[ix] == 0) {
            pos.reset();
            WTM_EGTB->pos_at_ix(pos, ix, WHITE);
            if (WTM_EGTB->pos_ix_is_used(pos,ix))
                check_consistency(pos, false);
        }
    }
    std::cout << "WTM Consistency check passed for draws" << std::endl;

    #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE)
    for (uint64_t ix = 0; ix < BTM_EGTB->num_pos; ix++) {
        EGPosition pos;
        if (BTM_EGTB->TB[ix] == 0) {
            pos.reset();
            BTM_EGTB->pos_at_ix(pos, ix, BLACK);
            if (BTM_EGTB->pos_ix_is_used(pos,ix))
                check_consistency(pos, false);
        }
    }
    std::cout << "BTM Consistency check passed for draws" << std::endl;
}

void GenEGTB::retrograde_promotion_tbs(int nthreads) {
    EGTB* egtb;
    EGTB* (*PROMO_EGTBS)[6];
    Color COLOR;

    for (int wtm = 0; wtm <= 1; ++wtm) {
        if (wtm) {
            COLOR = WHITE;
            PROMO_EGTBS = WTM_EGTBs;
            egtb = BTM_EGTB;
        } else {
            COLOR = BLACK;
            PROMO_EGTBS = BTM_EGTBs;
            egtb = WTM_EGTB;
        }
        
        int8_t V_FLIPS[8] = {0,  0, 56, 56,  0,  0, 56, 56};
        int8_t H_FLIPS[8] = {0,  7,  0,  7,  0,  7,  0,  7};
        int8_t SWAPS[8]   = {0,  0,  0,  0,  3,  3,  3,  3};

        PieceType capture_pt = NO_PIECE_TYPE;
        for (PieceType promote_pt = KNIGHT; promote_pt <= QUEEN; ++promote_pt) {
            if (PROMO_EGTBS[promote_pt][capture_pt] == NULL) { continue; }
            EGTB* PROMO_EGTB = PROMO_EGTBS[promote_pt][capture_pt];
            // query from compressed file
            PROMO_EGTB->init_compressed_tb(folder);
            assert (PROMO_EGTB->CTB->block_size == BLOCKSIZE);
            std::cout << "Retrograde from color=" << ((COLOR == WHITE) ? "W" : "B") << ", promotion=" << PieceToChar[promote_pt] << ", " << PROMO_EGTB->id << " with #positions=" << PROMO_EGTB->num_pos << std::endl;

            int N_SYMMETRIES = PROMO_EGTB->npawns > 0 ? 2 : 8; // symmetries of 

            #pragma omp parallel num_threads(nthreads)
            {
                DecompressCtx* dctx = new DecompressCtx(BLOCKSIZE);
            
                #pragma omp for schedule(static, BLOCKSIZE)
                for (uint64_t ix = 0; ix < PROMO_EGTB->num_nonep_pos; ix++) {
                    EGPosition pos;
                    EGPosition transformed_pos;

                    pos.reset();
                    PROMO_EGTB->pos_at_ix(pos, ix, COLOR);

                    int16_t val = PROMO_EGTB->get_value_dctx(ix, dctx);

                    // unused value in stored tb is 0, thus we have to check if this is a valid (used) position
                    if (val == 0 && !PROMO_EGTB->pos_ix_is_used(pos,ix)) continue;

                    int16_t lb = VAL_TO_LOWERBOUND(-val);

                    for (int t = 0; t < N_SYMMETRIES; t++) {
                        transformed_pos.reset();
                        transform_to(pos, transformed_pos, H_FLIPS[t], V_FLIPS[t], SWAPS[t]);

                        for (UndoInfo rev_move : EGUndoInfoList(transformed_pos, capture_pt, promote_pt)) {
                            transformed_pos.do_rev_move(rev_move);
                            uint64_t egbt_ix = egtb->ix_from_pos(transformed_pos);

                            // collisions should be very rare (e.g. two pawns that can promote)
                            #pragma omp atomic compare
                            if (egtb->TB[egbt_ix] < lb) { egtb->TB[egbt_ix] = lb; }
                            // if egtb->TB[egbt_ix] is unset (==0), then 0 < VAL_TO_LOWERBOUND(LOSS_IN(0))

                            transformed_pos.undo_rev_move(rev_move);
                        }

                    }
                }
            
                #pragma omp critical
                {
                    delete dctx;
                }
            }

            PROMO_EGTB->free_compressed_tb();
        }

    }
}


void GenEGTB::gen(int nthreads) {
    if (WTM_EGTB->exists(folder) && BTM_EGTB->exists(folder)) {
        std::cout << WTM_EGTB->id << " and " << BTM_EGTB->id << " already exist." << std::endl;
        return;
    }
    std::cout << "Generate " << WTM_EGTB->num_pos << " " << WTM_EGTB->id << " and " << BTM_EGTB->num_pos << " " << BTM_EGTB->id << std::endl;

    TimePoint t0 = now();

    // for now pawns we always load all tbs, else load promotion tbs dependent on disable_allocate_promotion_tb flag
    bool all_tb_dependencies_loaded = (WTM_EGTB->npawns + BTM_EGTB->npawns == 0) || !disable_allocate_promotion_tb;

    allocate();
    load_tb_dependencies(nthreads, all_tb_dependencies_loaded, true);
    std::cout << "MiB allocated: " << bytes_allocated / (1024*1024) << std::endl;
    std::cout << "MiB mmaped: " << bytes_mmaped / (1024*1024) << std::endl;

    TimePoint t1 = now();
    std::cout << "Finished allocate and load in " << (double) (t1 - t0) / 1000.0 << "s." << std::endl;

    int16_t LEVEL = 0;

    uint64_t N_LEVEL_POS = 0;

    EGTB* LOSS_EGTB;
    EGTB* WIN_EGTB;
    EGTB* (*CAPTURE_EGTBs)[6];
    Color LOSS_COLOR;
        
    int16_t MIN_LEVEL = 0;


    // Step 1. Get all infos from dependent tables.

    if (!all_tb_dependencies_loaded) {
        retrograde_promotion_tbs(nthreads);
    }

    for (int wtm = 0; wtm <= 1; ++wtm) {
        if (wtm) {
            LOSS_EGTB = WTM_EGTB;
            LOSS_COLOR = WHITE;
            CAPTURE_EGTBs = BTM_EGTBs;
        } else {
            LOSS_EGTB = BTM_EGTB;
            LOSS_COLOR = BLACK;
            CAPTURE_EGTBs = WTM_EGTBs;
        }

        uint64_t N_PIECE_COLLISION = 0;
        uint64_t N_SNTM_IN_CHECK = 0;
        uint64_t N_ILLEGAL_EP = 0;
        uint64_t N_SYMMETRY = 0;
        uint64_t N_CHECKMATE = 0;

        #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE) \
        reduction(+:N_LEVEL_POS) reduction(+:N_SYMMETRY) reduction(+:N_SNTM_IN_CHECK) \
        reduction(+:N_ILLEGAL_EP) reduction(+:N_PIECE_COLLISION) reduction(+:N_CHECKMATE) \
        reduction(max:MIN_LEVEL)
        for (uint64_t ix = 0; ix < LOSS_EGTB->num_pos; ix++) {
            EGPosition pos;
            pos.reset();
            LOSS_EGTB->pos_at_ix(pos, ix, LOSS_COLOR);

            if (popcount(pos.pieces()) != LOSS_EGTB->npieces) {
                N_PIECE_COLLISION++;
                LOSS_EGTB->TB[ix] = UNUSED;
                continue;
            } else if (pos.sntm_in_check()) {
                N_SNTM_IN_CHECK++;
                LOSS_EGTB->TB[ix] = UNUSED;
                continue;
            } else if ((ix >= LOSS_EGTB->num_nonep_pos) && !pos.check_ep(pos.ep_square())) {
                N_ILLEGAL_EP++;
                LOSS_EGTB->TB[ix] = UNUSED;
                continue;
            } else if (LOSS_EGTB->ix_from_pos(pos) != ix) {
                N_SYMMETRY++;
                LOSS_EGTB->TB[ix] = UNUSED;
                continue;
            } else if (!IS_LOWERBOUND(LOSS_EGTB->TB[ix])) {
                // can have lower bound already from retrograde_promotion_tbs
                LOSS_EGTB->TB[ix] = UNKNOWN;
            }

            EGMoveList movelist = EGMoveList(pos);
            if (movelist.size() == 0) {
                if (pos.stm_in_check()) {
                    LOSS_EGTB->TB[ix] = LOSS_IN(0);
                    N_CHECKMATE++;
                    N_LEVEL_POS++;
                } else {
                    LOSS_EGTB->TB[ix] = 0; // stale mate
                }

            } else {
                
                // can have lower bound already from retrograde_promotion_tbs
                int16_t max_val = IS_LOWERBOUND(LOSS_EGTB->TB[ix]) ? LOWERBOUND_TO_VAL(LOSS_EGTB->TB[ix]) : LOSS_IN(0);
                // int16_t lb = LOWERBOUND_TO_VAL(LOSS_EGTB->TB[ix]);
                // int16_t max_val_2 = LOSS_IN(0);

                bool has_full_eval = true;
                bool has_partial_eval = false;

                for (Move move : movelist) {
                    UndoInfo u = pos.do_move(move);
                    PieceType promotion = move.type_of() == PROMOTION ? move.promotion_type() : NO_PIECE_TYPE;
                    if (!u.captured && !promotion) {
                        // move stays in TB, cannot determine eval
                        has_full_eval = false;
                    } else {
                        has_partial_eval = true;
                        EGTB* cap_egtb = CAPTURE_EGTBs[promotion][u.captured];
                        if (!disable_allocate_promotion_tb || u.captured) {
                            uint64_t fwd_ix = cap_egtb->ix_from_pos(pos);
                            max_val = std::max(max_val, (int16_t) -cap_egtb->TB[fwd_ix]);
                        } // otherwise we must have lower bound from retrograde_promotion_tbs
                    }
                    pos.undo_move(u);
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
                    LOSS_EGTB->TB[ix] = max_val;
                    N_LEVEL_POS++;
                }
                if (has_partial_eval) {
                    if (max_val < 0) {
                        // there is at least one move that leads to a winning position for sntm
                        // max_val is the upper bound on the val when only considering moves to dependent positions
                        LOSS_EGTB->TB[ix] = MAYBELOSS_IN(max_val - LOSS_IN(0));
                    }
                    if (max_val >= 0) {
                        // known win and draw can be inferred from partial eval
                        LOSS_EGTB->TB[ix] = max_val;
                    }
                }
            }
            assert (!IS_LOWERBOUND(LOSS_EGTB->TB[ix]));
        }

        uint64_t N_UNUSED = N_SYMMETRY + N_ILLEGAL_EP + N_PIECE_COLLISION + N_SNTM_IN_CHECK;
        std::cout << "Stats for " << LOSS_EGTB->id << ":" << std::endl;
        std::cout << "    # Non-EP positions: " << LOSS_EGTB->num_nonep_pos << " (" << (double) LOSS_EGTB->num_nonep_pos / LOSS_EGTB->num_pos * 100 << "%)" << std::endl;
        std::cout << "    # EP positions: " << LOSS_EGTB->num_ep_pos << " (" << (double) LOSS_EGTB->num_ep_pos / LOSS_EGTB->num_pos * 100 << "%)" << std::endl;
        std::cout << "    Checkmate count: " << N_CHECKMATE << " (" << (double) N_CHECKMATE / LOSS_EGTB->num_pos * 100 << "%)" << std::endl;
        std::cout << "    " << N_UNUSED << " unused indices (" << (double) N_UNUSED / LOSS_EGTB->num_pos * 100 << "%)" << std::endl;
        std::cout << "    " << N_PIECE_COLLISION << " of which piece collision (" << (double) N_PIECE_COLLISION / LOSS_EGTB->num_pos * 100 << "%)" << std::endl;
        std::cout << "    " << N_SNTM_IN_CHECK << " of which sntm in check (" << (double) N_SNTM_IN_CHECK / LOSS_EGTB->num_pos * 100 << "%)" << std::endl;
        if (LOSS_EGTB->num_ep_pos > 0) std::cout << "    " << N_ILLEGAL_EP << " of which illegal EP positions (" << (double) N_ILLEGAL_EP / LOSS_EGTB->num_pos * 100 << "%)" << std::endl;
        std::cout << "    " << N_SYMMETRY << " of which symmetric positions (" << (double) N_SYMMETRY / LOSS_EGTB->num_pos * 100 << "%)" << std::endl;
    }

    TimePoint t2 = now();
    std::cout << "Finished init in " << (double) (t2 - t1) / 1000.0 << "s." << std::endl;


    std::cout << "MIN_LEVEL = " << int(MIN_LEVEL) << std::endl;

    // Step 2. Retrograde Analysis

    while (true) {
        LEVEL++;
        N_LEVEL_POS = 0;

        for (int wtm = 0; wtm <= 1; ++wtm) {
            if (wtm) {
                LOSS_COLOR = WHITE;
                LOSS_EGTB = WTM_EGTB;
                WIN_EGTB = BTM_EGTB;
            } else {
                LOSS_COLOR = BLACK;
                LOSS_EGTB = BTM_EGTB;
                WIN_EGTB = WTM_EGTB;
            }

            // Step 2a. From each lost position take a move backwards and set as win.

            #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE)
            for (uint64_t ix = 0; ix < LOSS_EGTB->num_pos; ix++) {
                EGPosition pos;
                if (LOSS_EGTB->TB[ix] == LOSS_IN(LEVEL-1)) {
                    pos.reset();
                    LOSS_EGTB->pos_at_ix(pos, ix, LOSS_COLOR);
                    assert (LOSS_EGTB->pos_ix_is_used(pos,ix));

                    for (UndoInfo rev_move : EGUndoInfoList(pos)) {
                        pos.do_rev_move(rev_move);
                        uint64_t win_ix = WIN_EGTB->ix_from_pos(pos);
                        if (!IS_SET(WIN_EGTB->TB[win_ix]) || WIN_EGTB->TB[win_ix] < WIN_IN(LEVEL) ) {
                            WIN_EGTB->TB[win_ix] = WIN_IN(LEVEL);
                        }
                        pos.undo_rev_move(rev_move);
                    }
                }
            }

            // Step 2b. For each new win position, take move backwards and flag as potential (maybe) loss

            #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE) reduction(+:N_LEVEL_POS)
            for (uint64_t win_ix = 0; win_ix < WIN_EGTB->num_pos; win_ix++) {
                if (WIN_EGTB->TB[win_ix] == WIN_IN(LEVEL)) {
                    EGPosition pos;
                    pos.reset();
                    WIN_EGTB->pos_at_ix(pos, win_ix, ~LOSS_COLOR); // not much slower
                    assert (WIN_EGTB->pos_ix_is_used(pos,win_ix));
                    N_LEVEL_POS++;

                    for (UndoInfo rev_move : EGUndoInfoList(pos)) {
                        pos.do_rev_move(rev_move);
                        uint64_t maybe_loss_ix = LOSS_EGTB->ix_from_pos(pos);
                        if (!IS_SET(LOSS_EGTB->TB[maybe_loss_ix])) {
                            if (LOSS_EGTB->TB[maybe_loss_ix] == UNKNOWN) {
                                LOSS_EGTB->TB[maybe_loss_ix] = MAYBELOSS_IN(LEVEL+1);
                            } else {
                                if (!(LOSS_EGTB->TB[maybe_loss_ix] - MAYBELOSS_IN(0) >= LEVEL+1)) {
                                    std::cout << "LEVEL=" << LEVEL << ", LOSS_EGTB->TB[maybe_loss_ix]=" << LOSS_EGTB->TB[maybe_loss_ix] << std::endl;
                                }
                                assert(LOSS_EGTB->TB[maybe_loss_ix] - MAYBELOSS_IN(0) >= LEVEL+1);
                                // LOSS_TB[maybe_loss_ix] has to be MAYBELOSS_IN(SOME_LEVEL) where SOME_LEVEL >= LEVEL+1
                                // if SOME_LEVEL == LEVEL+1 this is what we would have set anyways
                                // if SOME_LEVEL > LEVEL+1 there has to be a move to dependent table which is < WIN_IN(LEVEL)
                                // SOME_LEVEL < LEVEL + 1 cannot happen as all such positions had to be considered previous iterations
                            }
                        }
                        
                        pos.undo_rev_move(rev_move);
                    }
                }
            }            
        }


        std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;

        LEVEL++;

        // Step 2c. for each potenial (maybe) loss, check all forward moves to see if we can confirm loss

        N_LEVEL_POS = 0;
        for (int wtm = 0; wtm <= 1; ++wtm) {
            if (wtm) {
                LOSS_EGTB = WTM_EGTB;
                LOSS_COLOR = WHITE;
                WIN_EGTB = BTM_EGTB;
            } else {
                LOSS_EGTB = BTM_EGTB;
                LOSS_COLOR = BLACK;
                WIN_EGTB = WTM_EGTB;
            }

            #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE) reduction(+:N_LEVEL_POS)
            for (uint64_t ix = 0; ix < LOSS_EGTB->num_pos; ix++) {
                EGPosition pos;
                if (LOSS_EGTB->TB[ix] == MAYBELOSS_IN(LEVEL)) {
                    pos.reset();
                    LOSS_EGTB->pos_at_ix(pos, ix, LOSS_COLOR);
                    assert (LOSS_EGTB->pos_ix_is_used(pos,ix));

                    // check that all forward moves lead to checkmate in <= -(LEVEL-1)
                    EGMoveList moveList = EGMoveList(pos);
                    int16_t max_val = LOSS_IN(LEVEL-1); // there is at least one move that leads to WIN_IN(LEVEL-1)
                    if (moveList.size() == 0) {
                        max_val = 0; // has to be stale mate
                        if (LOSS_EGTB->TB[ix] == MAYBELOSS_IN(LEVEL))
                            std::cout << pos << ix << std::endl;
                        assert(LOSS_EGTB->TB[ix] != MAYBELOSS_IN(LEVEL)); // cannot happen at MAYBELOSS position
                    } else {
                        for (Move move : moveList) {
                            UndoInfo u = pos.do_move(move);
                            PieceType promotion = move.type_of() == PROMOTION ? move.promotion_type() : NO_PIECE_TYPE;

                            if (!promotion && !u.captured) {
                                uint64_t fwd_ix = WIN_EGTB->ix_from_pos(pos);
                                int16_t val = WIN_EGTB->TB[fwd_ix];
                                if (val == UNKNOWN) {
                                    max_val = 0;
                                } else {
                                    max_val = std::max(max_val, (int16_t) -val);
                                }
                            }

                            pos.undo_move(u);
                            
                            if (max_val > LOSS_IN(LEVEL-1)) { break; }
                        }
                    }
                    
                    if (max_val == LOSS_IN(LEVEL-1)) {
                        LOSS_EGTB->TB[ix] = LOSS_IN(LEVEL);
                        N_LEVEL_POS++;
                    } else {
                        // maybeloss can be refuted by finding non-capture non-promo move
                        LOSS_EGTB->TB[ix] = UNKNOWN;
                    }
                    
                }
            }
        }

        std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;

        if (LEVEL > MIN_LEVEL && N_LEVEL_POS == 0) { break; }
    }


    // Step 3. all entries that are not known wins or losses are draws, also set UNUSED to 0
    #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE)
    for (uint64_t ix = 0; ix < WTM_EGTB->num_pos; ix++) {
        if (WTM_EGTB->TB[ix] == UNKNOWN || WTM_EGTB->TB[ix] == UNUSED) WTM_EGTB->TB[ix] = 0;
        assert (IS_SET(WTM_EGTB->TB[ix]));
    }
    #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE)
    for (uint64_t ix = 0; ix < BTM_EGTB->num_pos; ix++) {
        if (BTM_EGTB->TB[ix] == UNKNOWN || BTM_EGTB->TB[ix] == UNUSED) BTM_EGTB->TB[ix] = 0;
        assert (IS_SET(BTM_EGTB->TB[ix]));
    }

    TimePoint t3 = now();
    std::cout << "Finished retrograde in " << (double) (t3 - t2) / 1000.0 << "s." << std::endl;
    
    // check_consistency_allocated_by_level(nthreads, LEVEL);


    // Step 4. In-memory Consistency checks (use if all tables can be loaded)

    if (all_tb_dependencies_loaded && do_consistency_checks) {
        TimePoint tt0 = now();
        // check_consistency_allocated_by_level(nthreads, LEVEL);
        check_consistency_allocated(nthreads);
        TimePoint tt1 = now();
        std::cout << "Finished in-memory consistency checks in " << (double) (tt1 - tt0)/ 1000.0 << "s." << std::endl;
    }


    // Step 5. Write to disk

    std::string cmd = "mkdir -p " + WTM_EGTB->get_folder(folder);
    system(cmd.c_str());
    if (WTM_EGTB->id == BTM_EGTB->id) {
        if (compress) {
            WTM_EGTB->compress_egtb(folder, nthreads, COMPRESSION_LEVEL, BLOCKSIZE, true);
        } else {
            WTM_EGTB->store_egtb(folder);
        }
        
        std::cout << "Stored " << WTM_EGTB->id << std::endl;
    } else {
        if (compress) {
            WTM_EGTB->compress_egtb(folder, nthreads, COMPRESSION_LEVEL, BLOCKSIZE, true);
            BTM_EGTB->compress_egtb(folder, nthreads, COMPRESSION_LEVEL, BLOCKSIZE, true);
        } else {
            WTM_EGTB->store_egtb(folder);
            BTM_EGTB->store_egtb(folder);
        }
        std::cout << "Stored " << WTM_EGTB->id << " and " << BTM_EGTB->id << std::endl;
    }
     

    TimePoint t4 = now();
    std::cout << "Wrote to disk in " << (double) (t4 - t3) / 1000.0 << "s." << std::endl;

    // compression corrupts WTM_EGTB->TB and BTM_EGTB->TB
    deallocate();

    // Step 6. Consistency checks from compressed files (use if all tables are too large)

    if (!all_tb_dependencies_loaded && do_consistency_checks) {
        TimePoint tt0 = now();
        check_consistency_max_bytes_allocated(nthreads, CONSISTENCY_CHECK_MAX_BYTES);
        TimePoint tt1 = now();
        std::cout << "Finished in-memory consistency checks in " << (double) (tt1 - tt0)/ 1000.0 << "s." << std::endl;
    }

    // Step 7. Clean up
    free_tb_dependencies();
    assert (bytes_allocated == 0);
    assert (bytes_mmaped == 0);

    TimePoint t6 = now();
    std::cout << "Finished TBs " << WTM_EGTB->id << " and " << BTM_EGTB->id << " in " << (double) (t6 - t0)/ 1000.0 << "s." << std::endl;

}

#endif