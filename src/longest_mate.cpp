#include <iostream>
#include <vector>
#include <fstream>
#include <prophet.h>
#include "misc.h"
#ifdef OMP
#include <omp.h>
#endif

struct CSVEntry {
    std::string egtb_id;
    uint64_t num_pos;
    uint64_t bytes;
    std::string fen;
    int dtm;
};


int main(int argc, char *argv[]) {
    assert (argc > 0);
    int nthreads = atoi(argv[1]);
    #ifndef OMP
    assert(nthreads == 1);
    #endif
    std::cout << "Probing with " << nthreads << " threads." << std::endl;


    std::string folder = "egtbs";
    init_prophet_tb(folder);
    
    uint64_t total_position_count = 0;
    
    std::vector<std::string> egtb_ids = get_egtb_identifiers();
    std::vector<CSVEntry> entries(egtb_ids.size());

    int count = 0;
    #pragma omp parallel for num_threads(nthreads) schedule(dynamic)
    for (uint i = 0; i < egtb_ids.size(); i++) {
        std::string egtb_id = egtb_ids[i];
        EGTB egtb = EGTB(egtb_id);
        #pragma omp atomic
        total_position_count += egtb.num_pos;

        if (!egtb.exists(folder)) {
            std::cout << egtb_id << " does not exist." << std::endl;
            continue;
        }
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
            entries[i] = {egtb_id, egtb.num_pos, egtb.CTB->compressed_filesize, "", -1};
        } else {
            EGPosition pos;
            pos.reset();
            egtb.pos_at_ix(pos, longest_mate_ix, WHITE);
            int mate_in_dtm = WIN_IN(0) - egtb.get_value(longest_mate_ix);
            entries[i] = {egtb_id, egtb.num_pos, egtb.CTB->compressed_filesize, pos.fen(), mate_in_dtm};
        }

        #pragma omp critical
        {
            count++;
            std::cout << "Finished " << count << "/" << egtb_ids.size() << ": " << entries[i].egtb_id << " " << entries[i].fen << " " << entries[i].dtm << std::endl;
        }

    }


    std::ofstream file("longest_mates.csv");
    file << "id,numpos,bytes,fen,dtm\n";

    for (uint i = 0; i < egtb_ids.size(); i++) {
        CSVEntry entry = entries[i];
        if (entry.dtm == -1) {
            std::cout << entry.egtb_id << " no win." << std::endl;
            file << entry.egtb_id << "," << entry.num_pos << "," << entry.bytes << ",,\n";
        } else {
            std::cout << entry.egtb_id << " " << entry.fen << " mate in " << entry.dtm << " dtm (" << entry.dtm / 2 + 1 << " moves) " << std::endl;
            file << entry.egtb_id << "," << entry.num_pos << "," << entry.bytes << "," << entry.fen << "," << entry.dtm << "\n";
        }
    }

    file.close();

    std::cout << "total_position_count: " << total_position_count << std::endl;
}