#include <iostream>
#include <vector>
#include <prophet_common.h>
#include <prophet.h>
#include "misc.h"
#include <math.h>
#ifdef OMP
#include <omp.h>
#endif
#include <filesystem>
namespace fs = std::filesystem;


int main(int argc, char *argv[]) {
    assert (argc == 3);
    std::string folder = argv[1];
    int nthreads = atoi(argv[2]);
    #ifndef OMP
    assert(nthreads == 1);
    #endif

    prophet_tb_init();

    int filecount = prophet_tb_add_path(folder.c_str());
    std::cout << "filecount: " << filecount << std::endl;
    TimePoint t0 = now();
    std::vector<fs::path> egtb_paths;
    for (const auto & entry : fs::recursive_directory_iterator(folder)) {
        if (entry.path().extension() == COMP_EXT) {
            std::string filename = entry.path().filename().u8string();
            std::string egtb_id = filename.substr(0, filename.find_first_of("."));
            egtb_paths.push_back(entry.path());
        }
    }

    // cheap structural check without decompressing: header is readable, num_pos is correct and
    // the file size equals the size of header, block size table and all blocks (e.g. not truncated, no overflow in block size table)
    // has to be done before mmapping, because reading the block sizes of a truncated file may crash
    std::vector<char> complete(egtb_paths.size());
    uint64_t n_incomplete = 0;
    for (uint i = 0; i < egtb_paths.size(); i++) {
        fs::path egtb_path = egtb_paths[i];
        std::string egtb_filename = egtb_path.filename().u8string();
        std::string egtb_id = egtb_filename.substr(0, egtb_filename.find_first_of("."));
        EGTB egtb = EGTB(egtb_path.parent_path().u8string(), egtb_id);
        int part = part_from_filename(egtb_filename);
        uint64_t expected_num_pos = part >= 0 && egtb.split() && part < egtb.n_parts() ? egtb.part_size(part) : egtb.num_pos;
        if ((part >= 0) != egtb.split() || part >= egtb.n_parts()) expected_num_pos = 0; // part file of a table that is not split or vice versa

        uint64_t checksum, num_pos, block_size;
        if (!read_compressed_header(egtb_path.u8string(), checksum, num_pos, block_size)) {
            std::cout << egtb_filename << " INCOMPLETE: could not read header." << std::endl;
        } else if (num_pos != expected_num_pos) {
            std::cout << egtb_filename << " INCOMPLETE: header has " << num_pos << " positions, expected " << expected_num_pos << "." << std::endl;
        } else if (!is_complete_compressed_file(egtb_path.u8string())) {
            std::cout << egtb_filename << " INCOMPLETE: file size does not match block size table." << std::endl;
        } else {
            complete[i] = true;
        }
        if (!complete[i]) n_incomplete++;
    }
    std::cout << "Structural check: " << egtb_paths.size() - n_incomplete << "/" << egtb_paths.size() << " files complete." << std::endl;

    if (n_incomplete == 0) {
        prophet_tb_load_all_files();
        size_t compressed_filesize = prophet_tb_get_size_on_disk_of_loaded_files();
        std::cout << "Init ProphetTB: Loaded " << filecount << " tables (" << (int) ceil((double) compressed_filesize / (1024*1024*1024)) << "GiB)" << std::endl;
    } else {
        std::cout << "Skip loading ProphetTB, because there are incomplete files." << std::endl;
    }

    bool all_ok = true;
    int count = 0;
    #pragma omp parallel for num_threads(nthreads) schedule(dynamic) reduction(&&:all_ok)
    for (uint i = 0; i < egtb_paths.size(); i++) {
        fs::path egtb_path = egtb_paths[i];
        std::string egtb_filename = egtb_path.filename().u8string();
        if (!complete[i]) {
            all_ok = false;
            #pragma omp critical
            {
                count++;
                std::cout << "Finished check " << count << "/" << egtb_paths.size() << ": " << egtb_filename << " ERROR (incomplete)" << std::endl;
            }
            continue;
        }
        // every file (part) is checked on its own
        CompressedTB ctb(0, egtb_path.u8string());
        bool ok = ctb.check_integrity(1);
        all_ok = all_ok && ok;

        #pragma omp critical
        {
            count++;
            std::cout << "Finished check " << count << "/" << egtb_paths.size() << ": " << egtb_filename << (ok ? " OK" : " ERROR") << " (v" << ctb.version << ")" << std::endl;
        }

    }
    
    TimePoint t1 = now();
    std::cout << "Finished in " << (t1-t0) / 1000 << " seconds." << std::endl;
    if (all_ok)
        std::cout << "All ok!" << std::endl;
    else
        std::cout << "There were some errors :(" << std::endl;

}