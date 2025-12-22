#include <iostream>
#include <vector>
#include "bitboard.h"
#include "values.h"
#include "kkx.h"
#include "triangular_indexes.h"
#include "egtb.h"
#include "eg_movegen.h"
#include <fstream>

int main() {
    Bitboards::init();
    init_kkx_table();
    init_kkp_table();
    init_tril();

    std::string folder = "egtbs";
    std::vector<std::string> egtb_ids = get_egtb_identifiers(0, 4);
    std::cout << "EGTB count: " << egtb_ids.size() << std::endl;

    uint64_t total_position_count = 0;
    int count = 0;
    
    std::ofstream file("longest_mates.csv");
    file << "id,fen,plies\n";

    for (std::string egtb_id : egtb_ids) {
        count++;
        EGTB egtb = EGTB(egtb_id);
        std::cout << count << ". " << egtb_id  << ": " << egtb.num_pos << std::endl;
        total_position_count += egtb.num_pos;

        if (!egtb.exists(folder)) {
            std::cout << "NOT FOUND." << std::endl;
            continue;
        }
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
            std::cout << "no win." << std::endl;
            file << egtb_id  << ",,\n";
        } else {
            EGPosition pos;
            pos.reset();
            egtb.pos_at_ix(pos, longest_mate_ix, WHITE);
            int mate_in_plies = WIN_IN(0) - egtb.get_value(longest_mate_ix);
            std::cout << longest_mate_ix << " " << pos.fen() << " mate in " << mate_in_plies << " plies (" << mate_in_plies / 2 + 1 << " moves) " << std::endl;
            file << egtb_id  << "," << pos.fen() << "," << mate_in_plies << "\n";
        }

        // egtb.free_tb();

    }


    std::cout << "total_position_count: " << total_position_count << std::endl;
}