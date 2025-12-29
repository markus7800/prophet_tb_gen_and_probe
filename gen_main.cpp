#include <iostream>
#include <vector>
#include "bitboard.h"
#include "kkx.h"
#include "linearize.h"
#include "eg_position.h"
#include "eg_movegen.h"
#include "gen_egtb.h"
#include "triangular_indexes.h"
#include <unordered_set>
#include <omp.h>


int main(int argc, char *argv[]) {
    Bitboards::init();
    init_kkx_table();
    init_kkp_table();
    init_tril();

    assert (argc > 0);
    int nthreads = atoi(argv[1]);

    std::string folder = "tmp_egtbs";
    std::vector<std::string> egtb_ids = get_egtb_identifiers(0, 2);
    std::cout << "EGTB count: " << egtb_ids.size() << std::endl;

    bool generate_missing = true;
    bool generate_only_one = false;
    bool do_consistency_checks = true;

    uint64_t count = 0;
    for (std::string egtb_id : egtb_ids) {
        EGTB egtb = EGTB(egtb_id);
        count++;
        std::cout << count << ". " << egtb.id << std::endl;
        if (generate_missing && !egtb.exists(folder)) {
            bool disable_allocate_promotion_tb = (egtb.npieces == 6) || true;

            GenEGTB g = GenEGTB(egtb.stm_pieces, egtb.sntm_pieces, folder, do_consistency_checks, disable_allocate_promotion_tb);
            g.gen(nthreads);
            if (generate_only_one) return 0;

        } else {
            std::cout << "Already exists." << std::endl;
        }
    }
    
    return 0;
}