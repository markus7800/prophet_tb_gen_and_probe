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
#include "pos_decoder.h"
#include <mutex>

std::string get_default_folder(std::string root_folder, int stm_pieces_[6], int sntm_pieces_[6]);
std::string get_default_folder_for_id(std::string root_folder, std::string egtb_id);

// part of a compressed file name [id].p[k].egtb.bz, -1 for [id].egtb.bz (table that is not split)
inline int part_from_filename(const std::string& filename) {
    size_t dot = filename.find_first_of(".");
    if (dot == std::string::npos || dot + 2 >= filename.size() || filename[dot + 1] != 'p') return -1;
    size_t end = filename.find_first_of(".", dot + 1);
    if (end == std::string::npos) return -1;
    return std::stoi(filename.substr(dot + 2, end - dot - 2));
}

struct EGTB {
    std::string folder; // does not end with "/"
    std::string id;
    int16_t* TB;
    CompressedTB* CTBs[MAX_PARTS]; // compressed file of every part (nullptr if not opened or missing)
    int stm_pieces[6];
    int sntm_pieces[6];
    int npieces;
    int npawns;
    TableIndex index;
    uint64_t num_nonep_pos; // == index.num_nonep_pos
    uint64_t num_ep_pos;    // == index.num_ep_pos
    uint64_t num_pos;       // == index.num_pos
    size_t filesize;
    bool mmaped;   // TB is a mmaped (decompressed) file
    bool anon;     // TB is an anonymous mapping of the whole index range, only the loaded parts use memory
    bool compressed;
    bool loaded;
    uint32_t loaded_parts;   // bit p is set if TB holds the values of part p
    uint64_t resident_bytes; // memory used by the loaded parts

    std::once_flag has_initialised_compressed_tb;

    // index layout for fast decoding (PosDecoder), created on first use
    IndexLayout* layout;
    std::once_flag has_initialised_layout;

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
        delete layout;
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
        build_table_index(stm_pieces, sntm_pieces, index);
        num_nonep_pos = index.num_nonep_pos;
        num_ep_pos = index.num_ep_pos;
        num_pos = index.num_pos;
        loaded = false;
        mmaped = false;
        anon = false;
        compressed = false;
        loaded_parts = 0;
        resident_bytes = 0;
        TB = nullptr;
        for (int p = 0; p < MAX_PARTS; p++) CTBs[p] = nullptr;
        layout = nullptr;
    }

    // thread-safe
    const IndexLayout* get_layout() {
        std::call_once(has_initialised_layout, [this]() {
            layout = new IndexLayout(index);
        });
        return layout;
    }

    // ---- parts (a table that is not split has one part covering all indices)
    bool split() const { return index.split; }
    int n_parts() const { return index.n_parts; }
    uint64_t part_start(int p) const { return index.parts[p].start; }
    uint64_t part_end(int p) const { return index.part_end(p); }
    uint64_t part_size(int p) const { return part_end(p) - part_start(p); }
    int part_of(uint64_t ix) const { return index.part_of(ix); }
    // is the value at ix in memory?
    bool ix_loaded(uint64_t ix) const { return loaded && (loaded_parts >> part_of(ix)) & 1; }

    void pos_at_ix(EGPosition &pos, uint64_t ix, Color stm) {
        assert (ix < this->num_pos);
        pos_at_ix_(pos, ix, stm, this->index);
    }

    uint64_t ix_from_pos(EGPosition const &pos) {
        uint64_t ix = ix_from_pos_(pos, this->index);
        assert (ix < this->num_pos);
        return ix;
    }

    bool pos_ix_is_used(EGPosition const &pos, uint64_t ix) {
        if (popcount(pos.pieces()) != npieces) {
            return false;
        } else if (pos.sntm_in_check()) {
            return false;
        } else if (index.is_ep(ix) && !pos.check_ep(pos.ep_square())) {
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
            assert (ix_loaded(ix));
            return this->TB[ix];
        } else if (compressed) {
            int p = part_of(ix);
            return this->CTBs[p]->get_value(ix - part_start(p));
        } else {
            perror(("Tried to get value on unloaded EGTB " + this->id).c_str());
            exit(EXIT_FAILURE);
        }
    }
    int16_t query_postion(EGPosition const &pos) {
        return this->get_value(this->ix_from_pos(pos));
    }

    // thread-safe
    int16_t get_value_dctx(uint64_t ix, DecompressCtx* dctx) {
        if (loaded) {
            assert (ix_loaded(ix));
            return this->TB[ix];
        } else if (compressed) {
            int p = part_of(ix);
            return this->CTBs[p]->get_value_dctx(ix - part_start(p), dctx);
        } else {
            perror(("Tried to get value on unloaded EGTB " + this->id).c_str());
            exit(EXIT_FAILURE);
        }
    }
    // thread-safe
    int16_t query_postion_dctx(EGPosition const &pos, DecompressCtx* dctx) {
        return this->get_value_dctx(this->ix_from_pos(pos), dctx);
    }

    // decompressed (raw int16) file, only for tables that are not split
    std::string get_filename() {
        assert (!folder.empty() && folder.back() != '/');
        return folder + "/" +  id + ".egtb";
    }

    // compressed file of part p: [id].egtb.bz if the table is not split, otherwise [id].p[p].egtb.bz
    std::string compressed_filename(int p) {
        assert (!folder.empty() && folder.back() != '/');
        if (!split()) return get_filename() + COMP_EXT;
        return folder + "/" + id + ".p" + std::to_string(p) + ".egtb" + COMP_EXT;
    }

    bool exists_part(int p) {
        return std::ifstream(compressed_filename(p)).good();
    }

    bool exists_compressed() {
        for (int p = 0; p < n_parts(); p++) {
            if (!exists_part(p)) return false;
        }
        return true;
    }

    bool exists_decompressed() {
        if (split()) return false;
        std::string filename = this->get_filename();
        return std::ifstream(filename).good();
    }

    bool exists() {
        return this->exists_decompressed() || this->exists_compressed();
    }

    // sum of the sizes of the opened compressed files
    uint64_t compressed_filesize() {
        uint64_t size = 0;
        for (int p = 0; p < n_parts(); p++) {
            if (CTBs[p] != nullptr) size += CTBs[p]->compressed_filesize;
        }
        return size;
    }

    void store_egtb();
    void compress_egtb(int nthreads, int compression_level, uint64_t block_size, bool verbose);
    void compress_part(int p, int nthreads, int compression_level, uint64_t block_size, bool verbose);

    void mmap_egtb_from_file();
    void load_egtb_from_file();
    // opens the compressed files of all existing parts (that are not open yet)
    void init_compressed_tb();
    void free_compressed_tb() {
        for (int p = 0; p < MAX_PARTS; p++) {
            if (CTBs[p] != nullptr) {
                delete CTBs[p];
                CTBs[p] = nullptr;
            }
        }
        compressed = false;
    }
    void ensure_compressed_tb_initialised() {
        std::call_once(has_initialised_compressed_tb, &EGTB::init_compressed_tb, this);
    }
    // allocates TB (zero-initialised) for all indices, only the parts in parts_mask use memory
    void allocate_tb(uint32_t parts_mask);
    void load_egtb_from_compressed_file(int nthreads);
    // loads only the parts in parts_mask, TB covers all indices
    void load_parts_from_compressed_file(int nthreads, uint32_t parts_mask);
    void free_tb();

    void maybe_decompress_and_mmap_egtb() {
        if (!this->exists_decompressed()) {
            // have to decompress on disk, delete later!
            assert (!split());
            this->init_compressed_tb();
            this->CTBs[0]->decompress_to_file(this->get_filename());
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

#endif