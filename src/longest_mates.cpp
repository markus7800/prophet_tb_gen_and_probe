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
    std::string mate_line;
};

std::vector<Move> get_mate_line(EGPosition pos, DecompressCtx* dctx) {
    int16_t val = probe_position_dctx(pos, dctx);
    std::vector<Move> pv;
    while (val != LOSS_IN(0)) {
        val = -val;
        if (val > 0) val++;
        if (val < 0) val--;
        bool found = false;
        for (Move move : EGMoveList(pos)) {
            UndoInfo u = pos.do_move(move);
            int16_t move_val = probe_position_dctx(pos, dctx);
            pos.undo_move(u);
            if (move_val == val) {
                pv.push_back(move);
                pos.do_move(move);
                found = true;
                break;
            }
        }
        assert (found);
    }
    return pv;
}

int main(int argc, char *argv[]) {
    assert (argc == 5);
    std::string folder = argv[1];
    int max_npieces = atoi(argv[2]);
    int nthreads = atoi(argv[3]);
    bool compute_lines = atoi(argv[4]);
    #ifndef OMP
    assert(nthreads == 1);
    #endif
    assert (0 <= max_npieces && max_npieces <= 4);
    std::cout << "Probing " << folder << " with " << nthreads << " threads." << std::endl;
    if (compute_lines) {
        std::cout << "Compute all mate lines..." << std::endl;
    }

    init_prophet_tb(folder);
        
    std::vector<std::string> egtb_ids = get_egtb_identifiers(0, max_npieces);
    std::vector<CSVEntry> entries(egtb_ids.size());

    TimePoint t0 = now();

    int count = 0;
    #pragma omp parallel for num_threads(nthreads) schedule(dynamic)
    for (uint i = 0; i < egtb_ids.size(); i++) {
        std::string egtb_id = egtb_ids[i];
        EGTB egtb = EGTB(egtb_id);

        if (!egtb.exists(folder)) {
            #pragma omp critical
            {
                std::cout << egtb_id << " does not exist." << std::endl;
            }
            continue;
        }
        egtb.init_compressed_tb(folder);

        int16_t longest_mate = WIN_IN(0) + 1;
        uint64_t longest_mate_ix = 0;

        for (uint64_t ix = 0; ix < egtb.num_pos; ix++) {
            int16_t val = egtb.get_value(ix);
            if (0 < val && val < longest_mate) {
                longest_mate = val;
                longest_mate_ix = ix;
            }
        }

        if (longest_mate == WIN_IN(0) + 1) {
            entries[i] = {egtb_id, egtb.num_pos, egtb.CTB->compressed_filesize, "", -1, ""};
        } else {
            EGPosition pos;
            pos.reset();
            egtb.pos_at_ix(pos, longest_mate_ix, WHITE);
            int mate_in_dtm = WIN_IN(0) - egtb.get_value(longest_mate_ix);
            
            std::string mate_line = "";
            if (compute_lines) {
                std::ostringstream mate_line_os;
                DecompressCtx* dctx = new DecompressCtx();
                for (Move move : get_mate_line(pos, dctx)) {
                    mate_line_os << move_to_uci(move) << " " ;
                }
                mate_line = mate_line_os.str();

            }

            entries[i] = {egtb_id, egtb.num_pos, egtb.CTB->compressed_filesize, pos.fen(), mate_in_dtm, mate_line};
        }

        #pragma omp critical
        {
            count++;
            std::cout << "Finished " << count << "/" << egtb_ids.size() << ": " << entries[i].egtb_id << " " << entries[i].fen << " " << entries[i].dtm << std::endl;
        }

    }
    
    TimePoint t1 = now();
    std::cout << "Finished in " << (t1-t0) / 1000 << " seconds." << std::endl;


    std::ofstream file("longest_mates.csv");
    file << "id,numpos,bytes,fen,dtm,line\n";

    for (uint i = 0; i < egtb_ids.size(); i++) {
        CSVEntry entry = entries[i];
        if (entry.dtm == -1) {
            std::cout << entry.egtb_id << " no win." << std::endl;
            file << entry.egtb_id << "," << entry.num_pos << "," << entry.bytes << ",,,\n";
        } else {
            std::cout << entry.egtb_id << " " << entry.fen << " mate in " << entry.dtm << " dtm (" << entry.dtm / 2 + 1 << " moves) " << std::endl;
            file << entry.egtb_id << "," << entry.num_pos << "," << entry.bytes << "," << entry.fen << "," << entry.dtm << "," << entry.mate_line << "\n";
        }
    }

    file.close();
}