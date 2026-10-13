#include <iostream>
#include <vector>
#include "bitboard.h"
#include "kkx.h"
#include "linearize.h"
#include "eg_position.h"
#include "eg_movegen.h"
#include "triangular_indexes.h"
#include "gen_egtb.cpp"
#include <unordered_set>
#ifdef OMP
#include <omp.h>
#endif

#define CONSISTENCY_CHECK_MAX_BYTES 96636764160ULL
// == 90 GiB = 96GB
// override at compile time with make ZSTD_LEVEL=... BLOCKSIZE=...
#ifdef EGTB_COMPRESSION_LEVEL
#define COMPRESSION_LEVEL EGTB_COMPRESSION_LEVEL
#else
#define COMPRESSION_LEVEL 19
#endif
#ifdef EGTB_BLOCKSIZE
#define BLOCKSIZE EGTB_BLOCKSIZE
#else
#define BLOCKSIZE 32768
#endif
static_assert(BLOCKSIZE % 8 == 0, "BLOCKSIZE must be divisible by 8");
static_assert(1 <= COMPRESSION_LEVEL && COMPRESSION_LEVEL <= 22, "COMPRESSION_LEVEL must be in [1, 22]");

int main(int argc, char *argv[]) {
    Bitboards::init();
    init_kkx_table();
    init_kkp_table();
    init_tril();

    // build/gen_main.out [folder] [max_npieces] [threads] (optional: [target_id])
    // with target_id, only the target table and its (transitive) dependencies are generated
    assert (argc == 4 || argc == 5);
    std::string folder = argv[1];
    int max_npieces = atoi(argv[2]);
    int nthreads = atoi(argv[3]);
    assert (0 <= max_npieces && max_npieces <= 4);
    std::vector<std::string> egtb_ids = get_egtb_identifiers(0, max_npieces);
    if (argc == 5) {
        std::unordered_set<std::string> closure = get_dependency_closure(argv[4]);
        std::vector<std::string> filtered_ids;
        for (std::string egtb_id : egtb_ids) {
            if (closure.count(egtb_id)) filtered_ids.push_back(egtb_id);
        }
        egtb_ids = filtered_ids;
        std::cout << "Generating " << argv[4] << " and its dependencies." << std::endl;
    }
    std::cout << "EGTB count: " << egtb_ids.size() << std::endl;

    bool generate_missing = true;
    bool generate_only_one = false;
    bool do_consistency_checks = true;
    bool compress = true;

    uint64_t count = 0;
    for (std::string egtb_id : egtb_ids) {
        std::string egtb_folder = get_default_folder_for_id(folder, egtb_id);
        EGTB egtb = EGTB(egtb_folder, egtb_id);
        count++;
        std::cout << count << ". " << egtb.id << std::endl;
        if (generate_missing && !egtb.exists()) {
            bool disable_allocate_promotion_tb = (egtb.npieces == 6);

            GenEGTB g = GenEGTB(
                egtb.stm_pieces, egtb.sntm_pieces, folder,
                do_consistency_checks, disable_allocate_promotion_tb, compress,
                CONSISTENCY_CHECK_MAX_BYTES, COMPRESSION_LEVEL, BLOCKSIZE
            );
            g.gen(nthreads);
            if (generate_only_one) return 0;

        } else {
            std::cout << "Already exists." << std::endl;
            // tables are used as dependencies of later tables, which requires them to have block size BLOCKSIZE
            if (egtb.exists_compressed()) {
                std::string compressed_filename = egtb.get_filename() + COMP_EXT;
                uint64_t checksum, num_pos, block_size;
                if (!read_compressed_header(compressed_filename, checksum, num_pos, block_size) ||
                    num_pos != egtb.num_pos || !is_complete_compressed_file(compressed_filename)) {
                    std::cerr << compressed_filename << " is corrupt. Delete it to regenerate." << std::endl;
                    exit(1);
                }
                if (block_size != BLOCKSIZE) {
                    std::cout << "Recompress from block size " << block_size << " to " << BLOCKSIZE << "." << std::endl;
                    recompress_TB(compressed_filename, compressed_filename, nthreads, COMPRESSION_LEVEL, BLOCKSIZE, true);
                }
            }
        }
    }
    
    return 0;
}