#ifndef EGTB_H_INCLUDED
#define EGTB_H_INCLUDED

#include <string>
#include <fstream>
#include "eg_position.h"
#include "kkx.h"
#include "compressed_tb.h"
#include "linearize.h"
#include "types.h"
#include "egtb_ids.h"
#include <mutex>

namespace Prophet {

std::string get_default_folder(std::string root_folder, int stm_pieces_[6], int sntm_pieces_[6]);
std::string get_default_folder_for_id(std::string root_folder, std::string egtb_id);

struct EGTB {
    std::string folder; // does not end with "/"
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

    std::once_flag has_initialised_compressed_tb;

    EGTB(int stm_pieces_[6], int sntm_pieces_[6], std::string root_folder) {
        std::string folder_ = get_default_folder(root_folder, stm_pieces_, sntm_pieces_);
        std::string id_ = get_egtb_identifier(stm_pieces_, sntm_pieces_);

        init(folder_, id_, stm_pieces_, sntm_pieces_);
    }
    
    EGTB(std::string folder_, std::string egtb_id) {
        int stm_pieces_[6];
        int sntm_pieces_[6];
        egtb_id_to_pieces(egtb_id, stm_pieces_, sntm_pieces_);
        init(folder_, egtb_id, stm_pieces_, sntm_pieces_);
    }
    ~EGTB() {
        // std::cout << "~EGTB(" << id << ")" << std::endl;
        free_compressed_tb();
        assert (TB == nullptr);
    }

    void init(std::string folder_, std::string id_, int stm_pieces_[6], int sntm_pieces_[6]) {
        assert (id_ == get_egtb_identifier(stm_pieces_, sntm_pieces_));
        assert (!folder_.empty() && folder_.back() != '/');
        id = id_;
        folder = folder_;
        
        npieces = 2;
        for (int i = 0; i < 6; i++) {
            stm_pieces[i] = stm_pieces_[i];
            sntm_pieces[i] = sntm_pieces_[i];
            npieces += stm_pieces[i] + sntm_pieces[i];
        }
        npawns = stm_pieces[PAWN] + sntm_pieces[PAWN];
        compute_poscounts(stm_pieces, sntm_pieces, kntm_poscounts, num_nonep_pos, num_ep_pos, num_pos);
        loaded = false;
        compressed = false;
        TB = nullptr;
        CTB = nullptr;
    }


    void pos_at_ix(EGPosition &pos, uint64_t ix, Color stm) {
        assert (ix < this->num_pos);
        pos_at_ix_(pos, ix, stm, this->stm_pieces, this->sntm_pieces, this->kntm_poscounts);
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

    // not thread-safe
    int16_t get_value(uint64_t ix) {
        if (loaded) {
            return this->TB[ix];
        } else if (compressed) {
            return this->CTB->get_value(ix);
        } else {
            perror(("Tried to get value on unloaded EGTB " + this->id).c_str());
            exit(EXIT_FAILURE);
        }
    }
    int16_t query_postion(EGPosition const pos) {
        return this->get_value(this->ix_from_pos(pos));
    }

    // thread-safe
    int16_t get_value_dctx(uint64_t ix, DecompressCtx* dctx) {
        if (loaded) {
            return this->TB[ix];
        } else if (compressed) {
            return this->CTB->get_value_dctx(ix, dctx);
        } else {
            perror(("Tried to get value on unloaded EGTB " + this->id).c_str());
            exit(EXIT_FAILURE);
        }
    }
    // thread-safe
    int16_t query_postion_dctx(EGPosition const pos, DecompressCtx* dctx) {
        return this->get_value_dctx(this->ix_from_pos(pos), dctx);
    }

    std::string get_filename() {
        assert (!folder.empty() && folder.back() != '/');
        return folder + "/" +  id + ".egtb";
    }

    bool exists_compressed() {
        std::string filename = this->get_filename() + COMP_EXT;
        return std::ifstream(filename).good();
    }

    bool exists_decompressed() {
        std::string filename = this->get_filename();
        return std::ifstream(filename).good();
    }

    bool exists() {
        return this->exists_decompressed() || this->exists_compressed();
    }

    void store_egtb();
    void compress_egtb(int nthreads, int compression_level, uint64_t block_size, bool verbose);

    void mmap_egtb_from_file();
    void load_egtb_from_file();
    void init_compressed_tb();
    void free_compressed_tb() {
        if (compressed && CTB != nullptr) {
            delete CTB;
            CTB = nullptr;
            compressed = false;
        }
    }
    void ensure_compressed_tb_initialised() {
        std::call_once(has_initialised_compressed_tb, &EGTB::init_compressed_tb, this);
    }
    void load_egtb_from_compressed_file(int nthreads);
    void free_tb();

    void maybe_decompress_and_mmap_egtb() {
        if (!this->exists_decompressed()) {
            // have to decompress on disk, delete later!
            this->init_compressed_tb();
            this->CTB->decompress_to_file(this->get_filename());
        }
        this->mmap_egtb_from_file();
    }

    void maybe_decompress_and_load_egtb(int nthreads) {
        if (!this->exists_decompressed()) {
            // can decompress directly to memory
            this->load_egtb_from_compressed_file(nthreads);
        } else {
            this->load_egtb_from_file();
        }
    }
};

} // namespace Prophet

#endif
