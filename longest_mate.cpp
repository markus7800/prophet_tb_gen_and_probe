#include <iostream>
#include <vector>
#include "bitboard.h"
#include "kkx.h"
#include "linearize.h"
#include "eg_position.h"
#include "eg_movegen.h"
#include "gen_egtb.h"
#include "triangular_indexes.h"
#include "egtb_iterator.hpp"
#include <fstream>

int main() {
    Bitboards::init();
    init_kkx_table();
    init_kkp_table();
    init_tril();

    int pieces1[6] = {0, 0, 0, 0, 0, 0};
    int pieces2[6] = {0, 0, 0, 0, 0, 0};

    std::string folder = "egtbs";
    std::vector<std::string> egtb_ids = get_egtb_identifiers(0, 4, 0, 4);
    std::cout << "EGTB count: " << egtb_ids.size() << std::endl;
    // exit(1);

    uint64_t total_position_count = 0;
    int count = 0;
    
    std::ofstream file("mates.txt");

    for (std::string egtb_id : egtb_ids) {
        count++;
        id_to_pieces(egtb_id, pieces1, pieces2);
        EGTB egtb = EGTB(pieces1, pieces2);
        std::cout << count << ". " << egtb_id  << ": " << egtb.num_pos << std::endl;
        total_position_count += egtb.num_pos;

        if (egtb_exists(&egtb, folder)) {
            unzip_and_load_egtb(&egtb, folder, true);
        }

        int16_t longest_mate = WIN_IN(0) + 1;
        uint64_t longest_mate_ix = 0;
        for (uint64_t ix = 0; ix < egtb.num_pos; ix++) {
            int16_t val = egtb.TB[ix];
            assert (IS_SET(val));
            if (0 < val && val < longest_mate) {
                longest_mate = val;
                longest_mate_ix = ix;
            }
        }
        if (longest_mate == WIN_IN(0) + 1) {
            std::cout << "no win." << std::endl;
            file << count << ". " << egtb_id  << ": " << "no win." << "\n";
        } else {
            EGPosition pos;
            pos.reset();
            egtb.pos_at_ix(pos, longest_mate_ix, WHITE);
            int mate_in_plies = WIN_IN(0) - egtb.TB[longest_mate_ix];
            std::cout << pos.fen() << " mate in " << mate_in_plies << " plies (" << mate_in_plies / 2 + 1 << " moves)" << std::endl;
            
            file << count << ". " << egtb_id  << ": " << pos.fen() << " mate in " << mate_in_plies << " plies (" << mate_in_plies / 2 + 1 << " moves)" << "\n";
        }

        free_egtb(&egtb);

        if (egtb_exists_zipped(&egtb, folder))
            rm_unzipped_egtb(&egtb, folder);
    }
    std::cout << "total_position_count: " << total_position_count << std::endl;
}