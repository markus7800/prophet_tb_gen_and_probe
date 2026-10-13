
#include "egtb.h"
#include <iostream>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <string>
#include <unistd.h> // close
#include <fcntl.h> // open
#include <sys/mman.h> // mmap
#include <sys/stat.h> // file size

std::string get_default_folder(std::string root_folder, int stm_pieces[6], int sntm_pieces[6]) {
    assert (!root_folder.empty() && root_folder.back() != '/');
    int npieces = 2;
    for (int i = 0; i < 6; i++) {
        npieces += stm_pieces[i] + sntm_pieces[i];
    }
    int npawns = stm_pieces[PAWN] + sntm_pieces[PAWN];
    std::ostringstream os;
    os << root_folder << "/" << npieces << "men/" << npawns << "pawns";
    return os.str();
}
std::string get_default_folder_for_id(std::string root_folder, std::string egtb_id) {
    int stm_pieces[6];
    int sntm_pieces[6];
    egtb_id_to_pieces(egtb_id, stm_pieces, sntm_pieces);
    return get_default_folder(root_folder, stm_pieces, sntm_pieces);
}

void EGTB::store_egtb() {
    assert (!split()); // decompressed files are only supported for tables that are not split
    // make folder if it does not exist
    std::string cmd = "mkdir -p " + folder;
    system(cmd.c_str());

    std::string filename = this->get_filename();
    FILE *f = fopen(filename.c_str(), "wb");
    fwrite(this->TB, sizeof(int16_t), this->num_pos, f);
    fclose(f);
}

void EGTB::compress_egtb(int nthreads, int compression_level, uint64_t block_size, bool verbose) {
    for (int p = 0; p < n_parts(); p++) {
        compress_part(p, nthreads, compression_level, block_size, verbose);
    }
}

void EGTB::compress_part(int p, int nthreads, int compression_level, uint64_t block_size, bool verbose) {
    assert (loaded && ((loaded_parts >> p) & 1));
    block_compress_TB(this->TB + part_start(p), part_size(p),
        nthreads, compression_level, block_size,
        compressed_filename(p), true, verbose);
}


void EGTB::mmap_egtb_from_file() {
    assert (!this->loaded);
    assert (this->TB == nullptr);
    assert (!split());
    std::string filename = this->get_filename();
    struct stat st;
    stat(filename.c_str(), &st);
    int fd = open(filename.c_str(), O_RDONLY);

    if (fd == -1) {
        printf("Could not open file %s.\n", filename.c_str());
        exit(EXIT_FAILURE);
    }
    this->TB = (int16_t*) mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (this->TB == MAP_FAILED) {
        close(fd);
        perror("Error mmapping the file");
        exit(EXIT_FAILURE);
    }
    // std::cout << "mmap " << filename << " to " << TB << " with size " << st.st_size  << std::endl;
    this->mmaped = true;
    this->anon = false;
    this->loaded = true;
    this->loaded_parts = 1;
    this->filesize = st.st_size;
    this->resident_bytes = this->filesize;
    assert(this->filesize == this->num_pos * sizeof(int16_t));
    close(fd);
}

void EGTB::load_egtb_from_file() {
    assert (!this->loaded);
    assert (this->TB == nullptr);
    assert (!split());
    std::string filename = this->get_filename();
    this->TB = (int16_t*) calloc(this->num_pos, sizeof(int16_t));
    FILE *f = fopen(filename.c_str(), "rb");
    fread(this->TB, sizeof(int16_t), this->num_pos, f);
    fclose(f);

    this->mmaped = false;
    this->anon = false;
    this->loaded = true;
    this->loaded_parts = 1;
    this->filesize = this->num_pos * sizeof(int16_t);
    this->resident_bytes = this->filesize;
}


void EGTB::init_compressed_tb() {
    // opens the parts that are not open yet (parts may be written after a table was opened, e.g. during generation)
    {
        for (int p = 0; p < n_parts(); p++) {
            if (this->CTBs[p] != nullptr) continue;
            std::string filename = compressed_filename(p);
            if (!std::ifstream(filename).good()) continue; // missing part
            this->CTBs[p] = new CompressedTB(0, filename);
            if (this->CTBs[p]->num_pos != part_size(p)) {
                std::cerr << filename << " has " << this->CTBs[p]->num_pos << " positions, expected " << part_size(p) << "." << std::endl;
                exit(EXIT_FAILURE);
            }
            this->compressed = true;
        }
    }
}

void EGTB::allocate_tb(uint32_t parts_mask) {
    assert (!this->loaded);
    assert (this->TB == nullptr);
    // anonymous mapping of the whole index range: untouched pages do not use memory, so only the parts that are
    // written use memory, and indices of all parts keep their global value (MAP_NORESERVE: no overcommit check)
    this->filesize = this->num_pos * sizeof(int16_t);
    void* p = mmap(nullptr, std::max(this->filesize, (size_t) 1), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        perror(("Could not allocate " + this->id).c_str());
        exit(EXIT_FAILURE);
    }
    this->TB = (int16_t*) p;
    this->mmaped = false;
    this->anon = true;
    this->loaded = true;
    this->loaded_parts = parts_mask;
    this->resident_bytes = 0;
    for (int q = 0; q < n_parts(); q++) {
        if ((parts_mask >> q) & 1) this->resident_bytes += part_size(q) * sizeof(int16_t);
    }
}

void EGTB::load_parts_from_compressed_file(int nthreads, uint32_t parts_mask) {
    if (!this->compressed) this->init_compressed_tb();
    allocate_tb(parts_mask);
    for (int p = 0; p < n_parts(); p++) {
        if (!((parts_mask >> p) & 1)) continue;
        if (this->CTBs[p] == nullptr) {
            std::cerr << "Missing " << compressed_filename(p) << std::endl;
            exit(EXIT_FAILURE);
        }
        this->CTBs[p]->decompress_to_array(nthreads, this->TB + part_start(p));
    }
}

void EGTB::load_egtb_from_compressed_file(int nthreads) {
    load_parts_from_compressed_file(nthreads, (1u << n_parts()) - 1);
}

void EGTB::free_tb() {
    assert (this->loaded);
    if (this->mmaped || this->anon) {
        int unmap = munmap(this->TB, std::max(this->filesize, (size_t) 1));
        if (unmap == -1) {
            perror("Error munmapping the file");
            exit(EXIT_FAILURE);
        }
    } else {
        free(this->TB);
    }
    this->TB = nullptr;
    this->loaded = false;
    this->mmaped = false;
    this->anon = false;
    this->loaded_parts = 0;
    this->resident_bytes = 0;
}
