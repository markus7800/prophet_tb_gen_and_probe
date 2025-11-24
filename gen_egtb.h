
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

#define UNUSED 1111
#define UNKNOWN 1110
inline int16_t WIN_IN(int16_t level) { return 1000 - level; }
inline int16_t LOSS_IN(int16_t level) { return -1000 + level; }
inline int16_t MAYBELOSS_IN(int16_t level) { return -11000 + level; }
// a position is MAYBELOSS_IN(level) if there is a move to position that is WIN_IN(level-1) and all other moves are <=WIN_IN(level-1) (draw and loss is possible)
inline int16_t IS_SET(int16_t val) { return LOSS_IN(0) <= val && val <= WIN_IN(0); }

#define LOWERBOUND_OFFSET 11000
inline int16_t IS_LOWERBOUND(int16_t val) { return LOSS_IN(0) + LOWERBOUND_OFFSET <= val && val <= WIN_IN(0) + LOWERBOUND_OFFSET; };
inline int16_t VAL_TO_LOWERBOUND(int16_t val) { return val + LOWERBOUND_OFFSET; };
inline int16_t LOWERBOUND_TO_VAL(int16_t lb) { return lb - LOWERBOUND_OFFSET; };

#define CHUNKSIZE 2048
// 90 GB
#define CONSISTENCY_CHECK_MAX_BYTES 90000000000

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

struct EGTB {
    std::string id;
    int16_t* TB;
    int stm_pieces[6];
    int sntm_pieces[6];
    int npieces;
    int npawns;
    uint64_t kntm_poscounts[N_KKP + 1];
    uint64_t num_nonep_pos;
    uint64_t num_ep_pos;
    uint64_t num_pos;
    size_t filesize;
    bool mmaped;
    bool loaded;

    EGTB(int stm_pieces_[6], int sntm_pieces_[6]) {
        npieces = 2;
        for (int i = 0; i < 6; i++) {
            stm_pieces[i] = stm_pieces_[i];
            sntm_pieces[i] = sntm_pieces_[i];
            npieces += stm_pieces[i] + sntm_pieces[i];
        }
        npawns = stm_pieces[PAWN] + sntm_pieces[PAWN];
        id = get_egtb_identifier(stm_pieces, sntm_pieces);
        compute_poscounts(stm_pieces, sntm_pieces, kntm_poscounts, num_nonep_pos, num_ep_pos, num_pos);
        loaded = false;
    }

    void pos_at_ix(EGPosition &pos, uint64_t ix, Color stm) {
        assert (ix < this->num_pos);
        if (stm == WHITE) {
            pos_at_ix_(pos, ix, stm, this->stm_pieces, this->sntm_pieces, this->kntm_poscounts);
        } else {
            pos_at_ix_(pos, ix, stm, this->sntm_pieces, this->stm_pieces, this->kntm_poscounts);
        }
    }

    uint64_t ix_from_pos(EGPosition const &pos) {
        uint64_t ix = ix_from_pos_(pos, this->kntm_poscounts);
        assert (ix < this->num_pos);
        return ix;
    }

    bool pos_ix_is_used(EGPosition const &pos, uint64_t ix) {
        if (popcount(pos.pieces()) != npieces) {
            return false;
        } else if (pos.sntm_in_check()) {
            return false;
        } else if ((ix > num_nonep_pos) && !pos.check_ep(pos.ep_square())) {
            return false;
        } else if (ix_from_pos(pos) != ix) {
            return false;
        } else {
            return true;
        }
    }

    std::string get_folder(std::string root_folder) {
        assert (!root_folder.empty() && root_folder.back() != '/');
        std::ostringstream os;
        os << root_folder << "/" << npieces << "men/" << npawns << "pawns/";
        return os.str();
    }
    std::string get_filename(std::string root_folder) {
        std::ostringstream os;
        os << get_folder(root_folder) << id << ".egtb";
        return os.str();
    }
};


void store_egtb(EGTB* egtb, std::string root_folder) {
    // make folder if it does not exist
    std::string folder = egtb->get_folder(root_folder);
    std::string cmd = "mkdir -p " + folder;
    system(cmd.c_str());

    std::string filename = egtb->get_filename(root_folder);
    std::ofstream outputFileStream;
    outputFileStream.open(filename, std::ios::out|std::ios::binary);
    for(uint64_t i=0; i<egtb->num_pos; i++) {
        int16_t val = egtb->TB[i];
        if (val == UNUSED) val = 0;
        if (!IS_SET(val)) {
            std::cout << "store_egtb: corrupt value " << int(val) << " at " << i << std::endl;
            exit(1);
        }
        outputFileStream.write((char*) &val, sizeof(int16_t));
    }
}

void zip_egtb(EGTB* egtb, std::string root_folder) {
    std::string filename = egtb->get_filename(root_folder);
    std::string cmd = "zip -m " + filename + ".zip " + filename; // -m   move into zipfile (delete OS files)
    system(cmd.c_str());
}

void unzip_egtb(EGTB* egtb, std::string root_folder) {
    std::string filename = egtb->get_filename(root_folder);
    std::string cmd = "unzip -n " + filename; // -n  never overwrite existing files
    system(cmd.c_str());
}

void rm_unzipped_egtb(EGTB* egtb, std::string root_folder) {
    std::string filename = egtb->get_filename(root_folder);
    std::string cmd = "rm " + filename;
    system(cmd.c_str());
}

void rm_all_unzipped_egtbs(std::string folder) {
    std::string cmd = "find " + folder + "-type f -name \"*.egtb\" -delete";
    system(cmd.c_str());
}

void load_egtb_mmap(EGTB* egtb, std::string root_folder) {
    std::string filename = egtb->get_filename(root_folder);
    struct stat st;
    stat(filename.c_str(), &st);
    int fd = open(filename.c_str(), O_RDONLY);

    if (fd == -1) {
        printf("Could not open file %s.\n", filename.c_str());
        exit(EXIT_FAILURE);
    }
    egtb->TB = (int16_t*) mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (egtb->TB == MAP_FAILED) {
        close(fd);
        perror("Error mmapping the file");
        exit(EXIT_FAILURE);
    }
    // std::cout << "mmap " << filename << " to " << TB << " with size " << st.st_size  << std::endl;
    egtb->mmaped = true;
    egtb->loaded = true;
    egtb->filesize = st.st_size;
    assert(egtb->filesize == egtb->num_pos * sizeof(int16_t));
    close(fd);
}

void free_egtb_mmap(EGTB* egtb) {
    assert (egtb->loaded);
    assert (egtb->mmaped);
    int unmap = munmap(egtb->TB, egtb->filesize);
    if (unmap == -1) {
        perror("Error munmapping the file");
        exit(EXIT_FAILURE);
    }
    egtb->loaded = false;
}

void load_egtb_in_memory(EGTB* egtb, std::string root_folder) {
    std::string filename = egtb->get_filename(root_folder);
    std::ifstream inputFileStream;
    egtb->TB = (int16_t*) calloc(egtb->num_pos, sizeof(int16_t));
    inputFileStream.open(filename, std::ios::in|std::ios::binary);
    for(uint64_t i=0; i<egtb->num_pos; i++)
        inputFileStream.read((char*) &egtb->TB[i], sizeof(int16_t));
    egtb->mmaped = false;
    egtb->loaded = true;
    egtb->filesize = egtb->num_pos * sizeof(int16_t);
}

void free_egtb_in_memory(EGTB* egtb) {
    assert (egtb->loaded);
    assert (!egtb->mmaped);
    free(egtb->TB);
    egtb->loaded = false;
}


void load_egtb(EGTB* egtb, std::string root_folder, bool mmap) {
    if (mmap)
        load_egtb_mmap(egtb, root_folder);
    else
        load_egtb_in_memory(egtb, root_folder);
}


void free_egtb(EGTB* egtb) {
    if (egtb->mmaped)
        free_egtb_mmap(egtb);
    else
        free_egtb_in_memory(egtb);
}

bool egtb_exists_zipped(EGTB* egtb, std::string root_folder) {
    std::string filename = egtb->get_filename(root_folder);
    return std::ifstream(filename).good();
}

bool egtb_exists_unzipped(EGTB* egtb, std::string root_folder) {
    std::string filename = egtb->get_filename(root_folder);
    return std::ifstream(filename).good();
}

bool egtb_exists(EGTB* egtb, std::string root_folder) {
    return egtb_exists_unzipped(egtb, root_folder) || egtb_exists_zipped(egtb, root_folder);
}

void unzip_and_load_egtb(EGTB* egtb, std::string root_folder, bool mmap) {
    if (!egtb_exists_unzipped(egtb, root_folder))
        unzip_egtb(egtb, root_folder);
    load_egtb(egtb, root_folder, mmap);
}

class GenEGTB {
    std::string folder;
    int wpieces[6];
    int bpieces[6];
    int n_pieces;

    EGTB* WTM_EGTB;
    EGTB* BTM_EGTB;

    EGTB* WTM_EGTBs[6][6];
    EGTB* BTM_EGTBs[6][6];

    bool zip;
    bool do_consistency_checks;
    bool disable_allocate_promotion_tb;

    uint64_t bytes_allocated;
    uint64_t bytes_mmaped;

public:
    GenEGTB(int wpieces_[6], int bpieces_[6], std::string folder_, bool zip_, bool do_consistency_checks_, bool disable_allocate_promotion_tb_) {
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

        this->WTM_EGTBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = WTM_EGTB;
        this->BTM_EGTBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = BTM_EGTB;

        // captures
        for (PieceType capture_pt = PAWN; capture_pt <= QUEEN; ++capture_pt) {
            if (wpieces[capture_pt] > 0) {
                wpieces[capture_pt]--;
                EGTB* egtb = new EGTB(wpieces, bpieces);
                this->WTM_EGTBs[NO_PIECE_TYPE][capture_pt] = egtb;
                std::cout << " " << egtb->id << " for white " << PieceToChar[capture_pt] << " captured, white to move" << std::endl;
                wpieces[capture_pt]++;
            }
            if (bpieces[capture_pt] > 0) {
                bpieces[capture_pt]--;
                EGTB* egtb = new EGTB(bpieces,wpieces);
                this->BTM_EGTBs[NO_PIECE_TYPE][capture_pt] = egtb;
                std::cout << " " << egtb->id << " for black " << PieceToChar[capture_pt] << " captured, black to move"  << std::endl;
                bpieces[capture_pt]++;
            }
        }

        // capture promotions
        for (PieceType promote_pt = KNIGHT; promote_pt <= QUEEN; ++promote_pt) {
            for (PieceType capture_pt = KNIGHT; capture_pt <= QUEEN; ++capture_pt) {
                if (bpieces[PAWN] > 0 && wpieces[capture_pt] > 0) {
                    wpieces[capture_pt]--;
                    bpieces[PAWN]--;
                    bpieces[promote_pt]++;
                    EGTB* egtb = new EGTB(wpieces, bpieces);
                    this->WTM_EGTBs[promote_pt][capture_pt] = egtb;
                    std::cout << " " << egtb->id << " for white " << PieceToChar[capture_pt] << " captured with black promotion to " << PieceToChar[promote_pt] << ", white to move" << std::endl;
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
                    bpieces[capture_pt]++;
                    wpieces[PAWN]++;
                    wpieces[promote_pt]--;
                }
            }
        }

        // promotions
        for (PieceType promote_pt = KNIGHT; promote_pt <= QUEEN; ++promote_pt) {
            if (bpieces[PAWN] > 0) {
                bpieces[PAWN]--;
                bpieces[promote_pt]++;
                EGTB* egtb = new EGTB(wpieces, bpieces);
                this->WTM_EGTBs[promote_pt][NO_PIECE_TYPE] = egtb;
                std::cout << " " << egtb->id << " for black promotion to " << PieceToChar[promote_pt] << ", white to move" << std::endl;
                bpieces[PAWN]++;
                bpieces[promote_pt]--;
            }
            if (wpieces[PAWN] > 0) {
                wpieces[PAWN]--;
                wpieces[promote_pt]++;
                EGTB* egtb = new EGTB(bpieces, wpieces); 
                this->BTM_EGTBs[promote_pt][NO_PIECE_TYPE] = egtb;
                std::cout << " " << egtb->id << " for white promotion to " << PieceToChar[promote_pt] << ", black to move" << std::endl;
                wpieces[PAWN]++;
                wpieces[promote_pt]--;
            }
        }

        this->do_consistency_checks = do_consistency_checks_;
        this->zip = zip_;
        this->disable_allocate_promotion_tb = disable_allocate_promotion_tb_;

        this->bytes_allocated = 0;
        this->bytes_mmaped = 0;
    }

    ~GenEGTB() {
        assert(!WTM_EGTB->loaded);
        free(WTM_EGTB);
        this->WTM_EGTB = NULL;

        assert(!BTM_EGTB->loaded);
        free(BTM_EGTB);
        this->BTM_EGTB = NULL;

        this->WTM_EGTBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = NULL;
        this->BTM_EGTBs[NO_PIECE_TYPE][NO_PIECE_TYPE] = NULL;

        for (PieceType promote_pt = NO_PIECE_TYPE; promote_pt <= QUEEN; ++promote_pt) {
            for (PieceType capture_pt = NO_PIECE_TYPE; capture_pt <= QUEEN; ++capture_pt) {
                if (WTM_EGTBs[promote_pt][capture_pt] != NULL) {
                    assert (!WTM_EGTBs[promote_pt][capture_pt]->loaded);
                    free(WTM_EGTBs[promote_pt][capture_pt]);
                }
                if (BTM_EGTBs[promote_pt][capture_pt] != NULL) {
                    assert(!BTM_EGTBs[promote_pt][capture_pt]->loaded);
                    free(BTM_EGTBs[promote_pt][capture_pt]);
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
    void load_tb_dependencies(bool allocate_promotion_tb, bool verbose_when_disabled);
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

    // Mlock ensures the arrays are never swapped
    // if (mlock(this->WTM_EGTB->TB, WTM_EGTB->num_pos * sizeof(int16_t)) != 0) {
    //     perror("mlock failed");
    //     exit(1);
    // }
    // if (mlock(this->BTM_EGTB->TB, WTM_EGTB->num_pos * sizeof(int16_t)) != 0) {
    //     perror("mlock failed");
    //     exit(1);
    // }

    add_to_bytes_count(this->WTM_EGTB);
    add_to_bytes_count(this->BTM_EGTB);
}

void GenEGTB::load_tb_dependencies(bool allocate_promotion_tb, bool verbose_when_disabled) {
    EGTB* egtb;
    for (PieceType promote_pt = NO_PIECE_TYPE; promote_pt <= QUEEN; ++promote_pt) {
        for (PieceType capture_pt = NO_PIECE_TYPE; capture_pt <= QUEEN; ++capture_pt) {
            if (promote_pt == NO_PIECE_TYPE && capture_pt == NO_PIECE_TYPE) continue;

            bool mmap = false;

            egtb = this->WTM_EGTBs[promote_pt][capture_pt];
            if (egtb != NULL) {
                if (promote_pt && !capture_pt && !allocate_promotion_tb) {
                    if (verbose_when_disabled) std::cout << "Disabled loading " << egtb->id << " for black promotion to " << PieceToChar[promote_pt] << ", white to move" << std::endl;
                } else {
                    if (egtb->loaded) {
                        std::cout << "Already loaded ";
                    } else {
                        unzip_and_load_egtb(egtb, folder, mmap);
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
                        unzip_and_load_egtb(egtb, folder, mmap);
                        add_to_bytes_count(egtb);
                        std::cout << "Loaded ";
                    }
                    if (!promote_pt && capture_pt) std::cout << "Loaded " << egtb->id << " for black " << PieceToChar[capture_pt] << " captured, black to move"  << std::endl;
                    if (promote_pt && capture_pt)  std::cout << "Loaded " << egtb->id << " for black " << PieceToChar[capture_pt] << " captured with white promotion to " << PieceToChar[promote_pt] << ", black to move" << std::endl;
                    if (promote_pt && !capture_pt) std::cout << "Loaded " << egtb->id << " for white promotion to " << PieceToChar[promote_pt] << ", black to move" << std::endl;
                }
            }
        }
    }
}

void GenEGTB::deallocate() {
    // if (munlock(this->WTM_EGTB->TB, WTM_EGTB->num_pos * sizeof(int16_t)) != 0) {
    //     perror("munlock failed");
    //     exit(1);
    // }
    // if (munlock(this->BTM_EGTB->TB, WTM_EGTB->num_pos * sizeof(int16_t)) != 0) {
    //     perror("munlock failed");
    //     exit(1);
    // }

    if (WTM_EGTB->loaded) free_egtb(WTM_EGTB);
    if (BTM_EGTB->loaded) free_egtb(BTM_EGTB);

    sub_from_bytes_count(this->WTM_EGTB);
    sub_from_bytes_count(this->BTM_EGTB);
}

void GenEGTB::free_tb_dependencies() {
    EGTB* egtb;
    for (PieceType promote_pt = NO_PIECE_TYPE; promote_pt <= QUEEN; ++promote_pt) {
        for (PieceType capture_pt = NO_PIECE_TYPE; capture_pt <= QUEEN; ++capture_pt) {
            egtb = WTM_EGTBs[promote_pt][capture_pt];
            if (egtb != NULL) {
                if (egtb->loaded) {
                    sub_from_bytes_count(egtb);
                    free_egtb(egtb);
                }
            }
            egtb = BTM_EGTBs[promote_pt][capture_pt];
            if (egtb != NULL) {
                if (egtb->loaded) {
                    sub_from_bytes_count(egtb);
                    free_egtb(egtb);
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
    EGMoveList movelist = EGMoveList<FORWARD>(pos);
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

        pos.undo_move(move, u);
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
                assert (tb_val == 0 || tb_val == UNUSED);
                continue;
            }

            EGMoveList movelist = EGMoveList<FORWARD>(pos);
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
                    pos.undo_move(move, u);
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
    load_tb_dependencies(false, false); // load all capture and capture-promotion tables (if not already loaded)

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

        load_egtb_mmap(LOSS_EGTB, folder); // only mmapped egtb
        add_to_bytes_count(LOSS_EGTB);

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
            for (PieceType promote_pt = NO_PIECE_TYPE; promote_pt <= QUEEN; ++promote_pt) {
                EGTB* cap_egtb = CAPTURE_EGTBs[promote_pt][NO_PIECE_TYPE];
                if (cap_egtb != NULL && !checked[promote_pt]) {
                    all_checked = false;
                    load_egtb_in_memory(cap_egtb, folder);
                    add_to_bytes_count(cap_egtb);
                    if (bytes_allocated > max_bytes) break; // load at least one TB
                }
            }
            if (all_checked) break;

            // chunksize is divisible by 8 thus no race conditions on ix_m
            #pragma omp parallel for num_threads(nthreads) schedule(static,2048)
            for (uint64_t ix = 0; ix < LOSS_EGTB->num_pos; ix++) {
                EGPosition pos;
                pos.reset();
                LOSS_EGTB->pos_at_ix(pos, ix, LOSS_COLOR);

                uint64_t ix_m = ix / 8;
                uint64_t ix_of = ix % 8;


                int16_t tb_val = LOSS_EGTB->TB[ix];
                if (!LOSS_EGTB->pos_ix_is_used(pos, ix)) {
                    assert (tb_val == 0 || tb_val == UNUSED);
                    max_obtained[ix_m] |= (1 << ix_of);
                    continue;
                }
                
                EGMoveList movelist = EGMoveList<FORWARD>(pos);
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
                        pos.undo_move(move, u);
                    }

                    if (max_val > 0) max_val--;
                    if (max_val < 0) max_val++;

                    assert(max_val <= tb_val);
                    if (max_val == tb_val) {
                        max_obtained[ix_m] |= (1 << ix_of);
                    }
                }
            }

            std::cout << "Checked consistency for " << LOSS_EGTB->id << ": ";
            for (PieceType promote_pt = NO_PIECE_TYPE; promote_pt <= QUEEN; ++promote_pt) {
                EGTB* cap_egtb = CAPTURE_EGTBs[promote_pt][NO_PIECE_TYPE];
                if (cap_egtb != NULL && cap_egtb->loaded) {
                    sub_from_bytes_count(cap_egtb);
                    free_egtb_in_memory(cap_egtb);
                    checked[promote_pt] = true;
                    std::cout << cap_egtb->id << " ";
                }
            }
            std::cout << std::endl;
        }

        #pragma omp parallel for num_threads(nthreads) schedule(static,2048)
        for (uint64_t ix = 0; ix < n_max_obtained; ix++) {
            if (max_obtained[ix] != 0xff) {
                std::cout << ix << ": " << int(max_obtained[ix]) << std::endl;
            }
            assert (max_obtained[ix] == 0xff);
        }
        free(max_obtained);

        sub_from_bytes_count(LOSS_EGTB);
        free_egtb(LOSS_EGTB);
    }
}

// does not require unused flag
void GenEGTB::check_consistency_allocated_by_level(int nthreads, int16_t MAX_LEVEL) {
    for (int16_t LEVEL = 0; LEVEL <= MAX_LEVEL; LEVEL++) {
        #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE)
        for (uint64_t ix = 0; ix < WTM_EGTB->num_pos; ix++) {
            EGPosition pos;
            if ((WTM_EGTB->TB[ix] == LOSS_IN(LEVEL) || WTM_EGTB->TB[ix] == WIN_IN(LEVEL))) {
                pos.reset();
                WTM_EGTB->pos_at_ix(pos, ix, WHITE);
                assert (!pos.sntm_in_check());
                if (ix > WTM_EGTB->num_nonep_pos) assert (pos.check_ep(pos.ep_square()));
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
                assert (!pos.sntm_in_check());
                if (ix > BTM_EGTB->num_nonep_pos) assert (pos.check_ep(pos.ep_square()));
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
            assert (!pos.sntm_in_check());
            if (ix > WTM_EGTB->num_nonep_pos) assert (pos.check_ep(pos.ep_square()));
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
            assert (!pos.sntm_in_check());
            if (ix > BTM_EGTB->num_nonep_pos) assert (pos.check_ep(pos.ep_square()));
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
            unzip_and_load_egtb(PROMO_EGTB, folder, true);
            add_to_bytes_count(PROMO_EGTB);
            std::cout << "Retrograde from color=" << ((COLOR == WHITE) ? "W" : "B") << ", promotion=" << PieceToChar[promote_pt] << ", " << PROMO_EGTB->id << " with #positions=" << PROMO_EGTB->num_pos << std::endl;

            int N_SYMMETRIES = PROMO_EGTB->npawns > 0 ? 2 : 8; // symmetries of 

            #pragma omp parallel for num_threads(nthreads) schedule(static)
            for (uint64_t ix = 0; ix < PROMO_EGTB->num_nonep_pos; ix++) {
                EGPosition pos;
                EGPosition transformed_pos;

                pos.reset();
                PROMO_EGTB->pos_at_ix(pos, ix, COLOR);

                int16_t val = PROMO_EGTB->TB[ix];

                // unused value in stored tb is 0, thus we have to check if this is a valid (used) position
                if (val == 0 && !PROMO_EGTB->pos_ix_is_used(pos,ix)) continue;

                int16_t lb = VAL_TO_LOWERBOUND(-val);

                for (int t = 0; t < N_SYMMETRIES; t++) {
                    transformed_pos.reset();
                    transform_to(pos, transformed_pos, H_FLIPS[t], V_FLIPS[t], SWAPS[t]);

                    for (Move move : EGMoveList<REVERSE>(transformed_pos, capture_pt, promote_pt)) {
                        transformed_pos.do_rev_move(move, UndoInfo(capture_pt,SQ_NONE));
                        uint64_t egbt_ix = egtb->ix_from_pos(transformed_pos);

                        // collisions should be very rare (e.g. two pawns that can promote)
                        #pragma omp atomic compare
                        if (egtb->TB[egbt_ix] < lb) { egtb->TB[egbt_ix] = lb; }
                        // if egtb->TB[egbt_ix] is unset (==0), then 0 < VAL_TO_LOWERBOUND(LOSS_IN(0))
                        
                        if (egtb->num_ep_pos > 0 && transformed_pos.ep_square() == SQ_NONE) {
                            Bitboard ep_candiates = transformed_pos.ep_candidates();
                            while (ep_candiates) {
                                Square ep_sq = pop_lsb(ep_candiates);
                                if (transformed_pos.check_ep(ep_sq)) {
                                    transformed_pos.set_ep_square(ep_sq);
                                    uint64_t ep_win_ix = egtb->ix_from_pos(transformed_pos);
                                    #pragma omp atomic compare
                                    if (egtb->TB[ep_win_ix] < lb) { egtb->TB[ep_win_ix] = lb; }
                                    transformed_pos.set_ep_square(SQ_NONE);
                                }
                            }
                        }

                        transformed_pos.undo_rev_move(move);
                    }

                }
            }

            sub_from_bytes_count(PROMO_EGTB);
            free_egtb(PROMO_EGTB);
        }

    }
}


void GenEGTB::gen(int nthreads) {
    if (egtb_exists(WTM_EGTB, folder) && egtb_exists(BTM_EGTB, folder)) {
        std::cout << WTM_EGTB->id << " and " << BTM_EGTB->id << " already exist." << std::endl;
        return;
    }
    std::cout << "Generate " << WTM_EGTB->num_pos << " " << WTM_EGTB->id << " and " << BTM_EGTB->num_pos << " " << BTM_EGTB->id << std::endl;

    TimePoint t0 = now();

    bool allocate_promotion_tb = (WTM_EGTB->npawns + BTM_EGTB->npawns > 0) && !disable_allocate_promotion_tb;
    allocate();
    load_tb_dependencies(allocate_promotion_tb, true);
    std::cout << "Bytes allocated: " << bytes_allocated << std::endl;
    std::cout << "Bytes mmaped: " << bytes_mmaped << std::endl;

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

    if (!allocate_promotion_tb) {
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
            } else if ((ix > LOSS_EGTB->num_nonep_pos) && !pos.check_ep(pos.ep_square())) {
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

            EGMoveList movelist = EGMoveList<FORWARD>(pos);
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
                        // uint64_t fwd_ix = cap_egtb->ix_from_pos(pos);
                        // max_val_2 = std::max(max_val_2, (int16_t) -cap_egtb->TB[fwd_ix]);
                        if (!disable_allocate_promotion_tb || u.captured) {
                            uint64_t fwd_ix = cap_egtb->ix_from_pos(pos);
                            max_val = std::max(max_val, (int16_t) -cap_egtb->TB[fwd_ix]);
                        } // otherwise we must have lower bound from retrograde_promotion_tbs
                    }
                    pos.undo_move(move, u);
                }
                // if (max_val_2 != max_val) {
                //     std::cout << max_val_2 << " vs " << max_val << "(lb: " << lb << ")" << std::endl;
                //     check_consistency(pos, true);
                // }

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
        std::cout << "Stats for " << ((wtm) ? get_egtb_identifier(wpieces, bpieces) : get_egtb_identifier(bpieces, wpieces)) << ":\n";
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
                    assert (!pos.sntm_in_check());
                    if (ix > LOSS_EGTB->num_nonep_pos) assert (pos.check_ep(pos.ep_square()));

                    for (Move move : EGMoveList<REVERSE>(pos)) {
                        pos.do_rev_move(move);
                        uint64_t win_ix = WIN_EGTB->ix_from_pos(pos);
                        if (!IS_SET(WIN_EGTB->TB[win_ix]) || WIN_EGTB->TB[win_ix] < WIN_IN(LEVEL) ) {
                            WIN_EGTB->TB[win_ix] = WIN_IN(LEVEL);
                        }
                        if (WIN_EGTB->num_ep_pos > 0 && pos.ep_square() == SQ_NONE) {
                            Bitboard ep_candiates = pos.ep_candidates();
                            while (ep_candiates) {
                                Square ep_sq = pop_lsb(ep_candiates);
                                if (pos.check_ep(ep_sq)) {
                                    pos.set_ep_square(ep_sq);
                                    uint64_t ep_win_ix = WIN_EGTB->ix_from_pos(pos);
                                    if (!IS_SET(WIN_EGTB->TB[ep_win_ix]) || WIN_EGTB->TB[ep_win_ix] < WIN_IN(LEVEL) ) {
                                        WIN_EGTB->TB[ep_win_ix] = WIN_IN(LEVEL);
                                    }
                                    pos.set_ep_square(SQ_NONE);
                                }
                            }
                        }
                        pos.undo_rev_move(move);
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
                    assert (!pos.sntm_in_check());
                    if (win_ix > WIN_EGTB->num_nonep_pos) assert (pos.check_ep(pos.ep_square()));
                    N_LEVEL_POS++;

                    for (Move move : EGMoveList<REVERSE>(pos)) {
                        pos.do_rev_move(move);
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
                        if (LOSS_EGTB->num_ep_pos > 0 && pos.ep_square() == SQ_NONE) {
                            Bitboard ep_candiates = pos.ep_candidates();
                            while (ep_candiates) {
                                Square ep_sq = pop_lsb(ep_candiates);
                                if (pos.check_ep(ep_sq)) {
                                    pos.set_ep_square(ep_sq);
                                    uint64_t ep_maybe_loss_ix = LOSS_EGTB->ix_from_pos(pos);
                                    if (!IS_SET(LOSS_EGTB->TB[ep_maybe_loss_ix])) {
                                        if (LOSS_EGTB->TB[ep_maybe_loss_ix] == UNKNOWN) {
                                            LOSS_EGTB->TB[ep_maybe_loss_ix] = MAYBELOSS_IN(LEVEL+1);
                                        } else {
                                            assert(LOSS_EGTB->TB[ep_maybe_loss_ix] - MAYBELOSS_IN(0) >= LEVEL+1);
                                        }
                                    }
                                    pos.set_ep_square(SQ_NONE);
                                }
                            }
                        }
                        pos.undo_rev_move(move);
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
                    assert (!pos.sntm_in_check());
                    if (ix > LOSS_EGTB->num_nonep_pos) assert (pos.check_ep(pos.ep_square()));

                    // check that all forward moves lead to checkmate in <= -(LEVEL-1)
                    EGMoveList moveList = EGMoveList<FORWARD>(pos);
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

                            pos.undo_move(move, u);
                            
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


    // Step 3. all entries that are not known wins or losses are draws
    #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE)
    for (uint64_t ix = 0; ix < WTM_EGTB->num_pos; ix++) {
        if (WTM_EGTB->TB[ix] == UNKNOWN) WTM_EGTB->TB[ix] = 0;
        assert (IS_SET(WTM_EGTB->TB[ix] || WTM_EGTB->TB[ix] == UNUSED));
    }
    #pragma omp parallel for num_threads(nthreads) schedule(static,CHUNKSIZE)
    for (uint64_t ix = 0; ix < BTM_EGTB->num_pos; ix++) {
        if (BTM_EGTB->TB[ix] == UNKNOWN) BTM_EGTB->TB[ix] = 0;
        assert (IS_SET(BTM_EGTB->TB[ix] || BTM_EGTB->TB[ix] == UNUSED));
    }

    TimePoint t3 = now();
    std::cout << "Finished retrograde in " << (double) (t3 - t2) / 1000.0 << "s." << std::endl;
    
    // check_consistency_allocated_by_level(nthreads, LEVEL);


    // Step 4. Write to disk

    // sets UNUSED to 0
    store_egtb(WTM_EGTB, folder);
    store_egtb(BTM_EGTB, folder);
    std::cout << "Stored " << WTM_EGTB->id << " and " << BTM_EGTB->id << std::endl;

    // check file integrity (TODO: remove)
    EGTB WTM_EGTB_MMAP = EGTB(this->wpieces, this->bpieces);
    load_egtb(&WTM_EGTB_MMAP, folder, true);
    for (uint64_t ix = 0; ix < WTM_EGTB_MMAP.num_pos; ix++) {
        if (!IS_SET(WTM_EGTB_MMAP.TB[ix]))  {
            std::cout << "WTM corrupted entry at " << ix << WTM_EGTB_MMAP.TB[ix] << std::endl;
            exit(1);
        }
    }
    free_egtb(&WTM_EGTB_MMAP);
    std::cout << "Checked WTM integrity" << std::endl;

    EGTB BTM_EGTB_MMAP = EGTB(this->bpieces, this->wpieces);
    load_egtb(&BTM_EGTB_MMAP, folder, true);
    for (uint64_t ix = 0; ix < BTM_EGTB_MMAP.num_pos; ix++) {
        if (!IS_SET(BTM_EGTB_MMAP.TB[ix])) {
            std::cout << "BTM corrupted entry at " << ix << BTM_EGTB_MMAP.TB[ix] << std::endl;
            exit(1);
        }
    }
    free_egtb(&BTM_EGTB_MMAP);
    std::cout << "Checked BTM integrity" << std::endl;


    TimePoint t4 = now();
    std::cout << "Wrote to disk in " << (double) (t4 - t3) / 1000.0 << "s." << std::endl;
    

    // Step 5. Consistency checks

    if (allocate_promotion_tb && do_consistency_checks) {
        // check_consistency_allocated_by_level(nthreads, LEVEL);
        check_consistency_allocated(nthreads);
    }
    
    deallocate();

    if (!allocate_promotion_tb && do_consistency_checks) {
        check_consistency_max_bytes_allocated(nthreads, CONSISTENCY_CHECK_MAX_BYTES);
    }

    TimePoint t5 = now();
    std::cout << "Finished consistency checks in " << (double) (t5 - t4)/ 1000.0 << "s." << std::endl;

    free_tb_dependencies();
    assert (bytes_allocated == 0);
    assert (bytes_mmaped == 0);

    TimePoint t6 = now();
    std::cout << "Finished TBs " << WTM_EGTB->id << " and " << BTM_EGTB->id << " in " << (double) (t6 - t0)/ 1000.0 << "s." << std::endl;

    // Step 6. Compress files
    if (zip) {
        zip_egtb(WTM_EGTB, folder);
        zip_egtb(BTM_EGTB, folder);

        std::cout << "Zipped " << WTM_EGTB->id << ".zip and " << BTM_EGTB->id << ".zip" << std::endl;

        rm_all_unzipped_egtbs(folder + "/6men");
    }

    TimePoint t7 = now();
    if (zip) std::cout << "Compressed files in " << (double) (t7 - t6)/ 1000.0 << "s." << std::endl;



}

#endif