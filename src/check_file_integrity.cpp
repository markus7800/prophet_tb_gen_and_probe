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

using namespace Prophet;

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
    prophet_tb_load_all_files();
    size_t compressed_filesize = prophet_tb_get_size_on_disk_of_loaded_files();
    std::cout << "Init ProphetTB: Loaded " << filecount << " tables (" << (int) ceil((double) compressed_filesize / (1024*1024*1024)) << "GiB)" << std::endl;


    TimePoint t0 = now();
    std::vector<fs::path> egtb_paths;
    for (const auto & entry : fs::recursive_directory_iterator(folder)) {
        if (entry.path().extension() == COMP_EXT) {
            std::string filename = entry.path().filename().u8string();
            std::string egtb_id = filename.substr(0, filename.find_first_of("."));
            egtb_paths.push_back(entry.path());
        }
    }

    bool all_ok = true;
    int count = 0;
    #pragma omp parallel for num_threads(nthreads) schedule(dynamic) reduction(&&:all_ok)
    for (uint i = 0; i < egtb_paths.size(); i++) {
        fs::path egtb_path = egtb_paths[i];
        std::string egtb_folder = egtb_path.parent_path().u8string();
        std::string egtb_filename = egtb_path.filename().u8string();;
        std::string egtb_id = egtb_filename.substr(0, egtb_filename.find_first_of("."));
        EGTB egtb = EGTB(egtb_folder, egtb_id);

        if (!egtb.exists()) {
            #pragma omp critical
            {
                count++;
                std::cout << "Finished check " << count << "/" << egtb_paths.size() << ": " << egtb_id << " does not exist." << std::endl;
            }
            continue;
        }
        egtb.init_compressed_tb();
        bool ok = egtb.CTB->check_integrity(1);
        all_ok = all_ok && ok;

        #pragma omp critical
        {
            count++;
            std::cout << "Finished check " << count << "/" << egtb_paths.size() << ": " << egtb_id << (ok ? " OK" : " ERROR") << std::endl;
        }

    }
    
    TimePoint t1 = now();
    std::cout << "Finished in " << (t1-t0) / 1000 << " seconds." << std::endl;
    if (all_ok)
        std::cout << "All ok!" << std::endl;
    else
        std::cout << "There were some errors :(" << std::endl;

}