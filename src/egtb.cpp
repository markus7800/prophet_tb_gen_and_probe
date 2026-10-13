
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
    // make folder if it does not exist
    std::string cmd = "mkdir -p " + folder;
    system(cmd.c_str());

    std::string filename = this->get_filename();
    FILE *f = fopen(filename.c_str(), "wb");
    fwrite(this->TB, sizeof(int16_t), this->num_pos, f);
    fclose(f);
}

void EGTB::compress_egtb(int nthreads, int compression_level, uint64_t block_size, bool verbose) {
    block_compress_TB(this->TB, this->num_pos,
        nthreads, compression_level, block_size,
        this->get_filename() + COMP_EXT, true, verbose);
}


void EGTB::mmap_egtb_from_file() {
    assert (!this->loaded);
    assert (this->TB == nullptr);
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
    this->loaded = true;
    this->filesize = st.st_size;
    assert(this->filesize == this->num_pos * sizeof(int16_t));
    close(fd);
}

void EGTB::load_egtb_from_file() {
    assert (!this->loaded);
    assert (this->TB == nullptr);
    std::string filename = this->get_filename();
    this->TB = (int16_t*) calloc(this->num_pos, sizeof(int16_t));
    FILE *f = fopen(filename.c_str(), "rb");
    fread(this->TB, sizeof(int16_t), this->num_pos, f);
    fclose(f);

    this->mmaped = false;
    this->loaded = true;
    this->filesize = this->num_pos * sizeof(int16_t);
}


void EGTB::init_compressed_tb() {
    if (!this->compressed) {
        assert (this->CTB == nullptr);
        this->CTB = new CompressedTB(
            EGTB_ID_TO_IX[this->id],
            this->get_filename() + COMP_EXT
        );
        this->compressed = true;
        this->file_permuted = false;
        this->capture_dc = this->CTB->flags & FLAG_CAPTURE_DC;
        if (this->CTB->digit_order != 0) {
            if (!unpack_digit_order(this->CTB->digit_order, stm_pieces, sntm_pieces, file_slots)) {
                std::cerr << "Invalid digit order in " << this->get_filename() + COMP_EXT << std::endl;
                exit(EXIT_FAILURE);
            }
            this->file_permuted = !DigitCursor(stm_pieces, sntm_pieces, kntm_poscounts, num_nonep_pos, file_slots).identity;
        }
    }
}

void EGTB::load_egtb_from_compressed_file(int nthreads) {
    if (!this->compressed) this->init_compressed_tb();
    assert (this->CTB != nullptr);
    assert (!this->loaded);
    assert (this->TB == nullptr);
    this->TB = (int16_t*) calloc(this->num_pos, sizeof(int16_t));
    if (!this->file_permuted) {
        this->CTB->decompress_to_array(nthreads, TB);
    } else {
        // values are stored in file index order: decompress every block and move the values to their canonical index
        CompressedTB* ctb = this->CTB;
        uint64_t checksum = 0;
        bool corrupt = false;
        #pragma omp parallel num_threads(nthreads) reduction(+:checksum) reduction(||:corrupt)
        {
            DecompressCtx dctx(ctb->block_size);
            DigitCursor cursor(stm_pieces, sntm_pieces, kntm_poscounts, num_nonep_pos, file_slots);
            #pragma omp for schedule(static)
            for (uint64_t block_ix = 0; block_ix < ctb->n_blocks; block_ix++) {
                if (!ctb->decompress_block(block_ix, &dctx, dctx.uncompressed_buf)) { corrupt = true; continue; }
                uint64_t start = block_ix * ctb->block_size;
                for (uint64_t i = 0; i < ctb->get_block_npos(block_ix); i++) {
                    int16_t v = dctx.uncompressed_buf[i];
                    checksum += checksum_term(start + i, v); // checksum of the file (file index order)
                    TB[cursor.to_canonical(start + i)] = v;
                }
            }
        }
        if (corrupt || checksum != ctb->checksum) {
            std::cerr << "Corrupt file " << this->get_filename() + COMP_EXT << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    
    this->mmaped = false;
    this->loaded = true;
    this->filesize = this->num_pos * sizeof(int16_t);
}

void EGTB::free_tb() {
    assert (this->loaded);
    if (this->mmaped) {
        int unmap = munmap(this->TB, this->filesize);
        if (unmap == -1) {
            perror("Error munmapping the file");
            exit(EXIT_FAILURE);
        }
    } else {
        free(this->TB);
    }
    this->TB = nullptr;
    this->loaded = false;
}

