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

    assert (argc == 4);
    std::string folder = argv[1];
    int max_npieces = atoi(argv[2]);
    int nthreads = atoi(argv[3]);
    assert (0 <= max_npieces && max_npieces <= 4);
    std::vector<std::string> egtb_ids = get_egtb_identifiers(0, max_npieces);
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
        }
    }
    
    return 0;
}