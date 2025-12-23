#include <iostream>
#include <vector>
#include "bitboard.h"
#include "values.h"
#include "kkx.h"
#include "triangular_indexes.h"
#include "egtb.h"
#include "eg_movegen.h"
#include <fstream>
#include <omp.h>

struct CSVEntry {
    std::string egtb_id;
    std::string fen;
    int plies;
};


int main(int argc, char *argv[]) {
    Bitboards::init();
    init_kkx_table();
    init_kkp_table();
    init_tril();
    init_egtb_id_to_ix();

    assert (argc > 0);
    int nthreads = atoi(argv[1]);


    std::string folder = "egtbs";
    std::vector<std::string> egtb_ids = get_egtb_identifiers(0, 4);
    std::cout << "EGTB count: " << egtb_ids.size() << std::endl;

    uint64_t total_position_count = 0;
    
    std::vector<CSVEntry> entries(egtb_ids.size());

    #pragma omp parallel for num_threads(nthreads) schedule(dynamic)
    for (uint i = 0; i < egtb_ids.size(); i++) {
        std::string egtb_id = egtb_ids[i];
        EGTB egtb = EGTB(egtb_id);
        #pragma omp atomic
        total_position_count += egtb.num_pos;

        assert (egtb.exists(folder));

        // egtb.maybe_decompress_and_load_egtb(folder);
        egtb.init_compressed_tb(folder);

        int16_t longest_mate = WIN_IN(0) + 1;
        uint64_t longest_mate_ix = 0;

        for (uint64_t ix = 0; ix < egtb.num_pos; ix++) {
            int16_t val = egtb.get_value(ix);
            assert (IS_SET(val));
            if (0 < val && val < longest_mate) {
                longest_mate = val;
                longest_mate_ix = ix;
            }
        }

        if (longest_mate == WIN_IN(0) + 1) {
            entries[i] = {egtb_id, "", -1};
        } else {
            EGPosition pos;
            pos.reset();
            egtb.pos_at_ix(pos, longest_mate_ix, WHITE);
            int mate_in_plies = WIN_IN(0) - egtb.get_value(longest_mate_ix);
            entries[i] = {egtb_id, pos.fen(), mate_in_plies};
        }

        // egtb.free_tb();
    }


    std::ofstream file("longest_mates.csv");
    file << "id,fen,plies\n";

    for (uint i = 0; i < egtb_ids.size(); i++) {
        CSVEntry entry = entries[i];
        if (entry.plies == -1) {
            std::cout << entry.egtb_id << " no win." << std::endl;
            file << entry.egtb_id  << ",,\n";
        } else {
            std::cout << entry.egtb_id << " " << entry.fen << " mate in " << entry.plies << " plies (" << entry.plies / 2 + 1 << " moves) " << std::endl;
            file << entry.egtb_id  << "," << entry.fen << "," << entry.plies << "\n";
        }
    }

    file.close();

    std::cout << "total_position_count: " << total_position_count << std::endl;
}