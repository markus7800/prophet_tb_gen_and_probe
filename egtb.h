#ifndef EGTB_H_INCLUDED
#define EGTB_H_INCLUDED

#include <string>
#include <fstream>
#include "eg_position.h"
#include "kkx.h"
#include "compressed_tb.h"
#include "linearize.h"
#include "types.h"

std::string get_pieces_identifier(int pieces[6]);
std::string get_egtb_identifier(int stm_pieces[6], int sntm_pieces[6]);
void egtb_id_to_pieces(std::string egtb_id, int pieces1[6], int pieces2[6]);

struct EGTB {
    std::string id;
    int16_t* TB;
    CompressedTB* CTB;
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
    bool compressed;
    bool loaded;


    EGTB(int stm_pieces_[6], int sntm_pieces_[6]) {
        init(stm_pieces_, sntm_pieces_);
    }
    
    EGTB(std::string egtb_id) {
        int stm_pieces_[6];
        int sntm_pieces_[6];
        egtb_id_to_pieces(egtb_id, stm_pieces_, sntm_pieces_);
        init(stm_pieces_, sntm_pieces_);
    }
    ~EGTB() {
        if (compressed && CTB != nullptr) {
            delete CTB;
        }
        assert (TB == nullptr);
    }

    void init(int stm_pieces_[6], int sntm_pieces_[6]) {
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
        compressed = false;
        TB = nullptr;
        CTB = nullptr;
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
        } else if ((ix >= num_nonep_pos) && !pos.check_ep(pos.ep_square())) {
            return false;
        } else if (ix_from_pos(pos) != ix) {
            return false;
        } else {
            return true;
        }
    }

    int16_t get_value(uint64_t ix) {
        if (loaded) {
            return this->TB[ix];
        } else if (compressed) {
            return this->CTB->get(ix);
        } else {
            perror(("Tried to get value on unloaded EGTB " + this->id).c_str());
            exit(EXIT_FAILURE);
        }
    }
    int16_t query_postion(EGPosition const pos) {
        return this->get_value(this->ix_from_pos(pos));
    }

    std::string get_folder(std::string root_folder) {
        assert (!root_folder.empty() && root_folder.back() != '/');
        std::ostringstream os;
        os << root_folder << "/" << npieces << "men/" << npawns << "pawns/";
        return os.str();
    }
    
    std::string get_filename(std::string root_folder) {
        return get_folder(root_folder) + id + ".egtb";
    }

    bool exists_compressed(std::string root_folder) {
        std::string filename = this->get_filename(root_folder) + COMP_EXT;
        return std::ifstream(filename).good();
    }

    bool exists_decompressed(std::string root_folder) {
        std::string filename = this->get_filename(root_folder);
        return std::ifstream(filename).good();
    }

    bool exists(std::string root_folder) {
        return this->exists_decompressed(root_folder) || this->exists_compressed(root_folder);
    }

    void store_egtb(std::string root_folder);
    void mmap_egtb_from_file(std::string root_folder);
    void load_egtb_from_file(std::string root_folder);
    void init_compressed_tb(std::string root_folder);
    void load_egtb_from_compressed_file(std::string root_folder, int nthreads=1);
    void free_tb();

    void maybe_decompress_and_mmap_egtb(std::string root_folder) {
        if (!this->exists_decompressed(root_folder)) {
            // have to decompress on disk, delete later!
            this->init_compressed_tb(root_folder);
            this->CTB->decompress_to_file(this->get_filename(root_folder));
        }
        this->mmap_egtb_from_file(root_folder);
    }

    void maybe_decompress_and_load_egtb(std::string root_folder, int nthreads=1) {
        if (!this->exists_decompressed(root_folder)) {
            // can decompress directly to memory
            this->load_egtb_from_compressed_file(root_folder, nthreads);
        } else {
            this->load_egtb_from_file(root_folder);
        }
    }
};

#endif