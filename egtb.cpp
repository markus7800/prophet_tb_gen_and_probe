
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


void EGTB::store_egtb(std::string root_folder) {
    // make folder if it does not exist
    std::string folder = this->get_folder(root_folder);
    std::string cmd = "mkdir -p " + folder;
    system(cmd.c_str());

    std::string filename = this->get_filename(root_folder);
    FILE *f = fopen(filename.c_str(), "wb");
    fwrite(this->TB, sizeof(int16_t), this->num_pos, f);
    fclose(f);
}


void EGTB::mmap_egtb_from_file(std::string root_folder) {
    assert (!this->loaded);
    assert (this->TB == nullptr);
    std::string filename = this->get_filename(root_folder);
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

void EGTB::load_egtb_from_file(std::string root_folder) {
    assert (!this->loaded);
    assert (this->TB == nullptr);
    std::string filename = this->get_filename(root_folder);
    this->TB = (int16_t*) calloc(this->num_pos, sizeof(int16_t));
    FILE *f = fopen(filename.c_str(), "rb");
    fread(this->TB, sizeof(int16_t), this->num_pos, f);
    fclose(f);

    this->mmaped = false;
    this->loaded = true;
    this->filesize = this->num_pos * sizeof(int16_t);
}


void EGTB::init_compressed_tb(std::string root_folder) {
    if (!this->compressed) {
        assert (this->CTB == nullptr);
        this->CTB = new CompressedTB(
            EGTB_ID_TO_IX[this->id],
            this->get_filename(root_folder) + COMP_EXT
        );
        this->compressed = true;
    }
}

void EGTB::load_egtb_from_compressed_file(std::string root_folder, int nthreads) {
    if (!this->compressed) this->init_compressed_tb(root_folder);
    assert (this->CTB != nullptr);
    assert (!this->loaded);
    assert (this->TB == nullptr);
    this->TB = (int16_t*) calloc(this->num_pos, sizeof(int16_t));
    this->CTB->decompress_to_array(nthreads, TB);
    
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

