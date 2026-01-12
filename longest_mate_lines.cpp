#include <iostream>
#include <vector>
#include <fstream>
#include <unordered_map>
#include <prophet.h>
#include "misc.h"
#ifdef OMP
#include <omp.h>
#endif

std::vector<Move> get_mate_line(EGPosition pos, DecompressCtx* dctx) {
    int16_t val = probe_position_dctx(pos, dctx);
    std::vector<Move> pv;
    while (val != LOSS_IN(0)) {
        val = -val;
        if (val > 0) val++;
        if (val < 0) val--;
        bool found = false;
        for (Move move : EGMoveList<FORWARD>(pos)) {
            UndoInfo u = pos.do_move(move);
            int16_t move_val = probe_position_dctx(pos, dctx);
            pos.undo_move(move, u);
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

struct CSVEntry {
    std::string egtb_id;
    std::string num_pos_str;
    std::string bytes_str;
    std::string fen;
    std::string dtm_str;
    std::string mate_line;
};

#define LOAD_HALF 1

int main(int argc, char *argv[]) {
    assert (argc > 0);
    int nthreads = atoi(argv[1]);
    #ifndef OMP
    assert(nthreads == 1);
    #endif
    std::cout << "Probing with " << nthreads << " threads." << std::endl;


    std::string folder = "egtbs";
    init_prophet_tb(folder);
    

#if LOAD_HALF
    for (std::string egtb_id : get_egtb_identifiers(0, 4)) {
        std::string mirror_egtb_id = get_mirror_id(egtb_id);
        if (mirror_egtb_id != egtb_id) {
            if (id_to_egtb[egtb_id] == nullptr || id_to_egtb[mirror_egtb_id] == nullptr) continue;
            if (id_to_egtb[egtb_id]->CTB->compressed_filesize < id_to_egtb[mirror_egtb_id]->CTB->compressed_filesize) {
                delete id_to_egtb[mirror_egtb_id];
                id_to_egtb[mirror_egtb_id] = nullptr;
            } else {
                delete id_to_egtb[egtb_id];
                id_to_egtb[egtb_id] = nullptr;
            }
        }
    }
    uint64_t count = 0;
    uint64_t compressed_filesize = 0;
    for (std::string egtb_id : get_egtb_identifiers()) {
        if (id_to_egtb[egtb_id] != nullptr) {
            count++;
            compressed_filesize += id_to_egtb[egtb_id]->CTB->compressed_filesize;
        }
    }
    std::cout << "Reduced to " << count << " EGTBs (" << (int) ceil((double) compressed_filesize / (1024*1024*1024)) << "GiB)" << std::endl;
#endif

    
    std::ifstream file("longest_mates.csv");std::string line;

    std::vector<CSVEntry> entries;
    std::getline(file, line); // header
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string egtb_id, num_pos_str, bytes_str, fen, ply_str;

        std::getline(ss, egtb_id, ',');
        std::getline(ss, num_pos_str, ',');
        std::getline(ss, bytes_str, ',');
        std::getline(ss, fen, ',');
        std::getline(ss, ply_str, ',');

        entries.push_back({egtb_id, num_pos_str, bytes_str, fen, ply_str, ""});
    }
    file.close();

    TimePoint t0 = now();
    uint64_t probe_count = 0;
    #pragma omp parallel num_threads(nthreads)
    {
        DecompressCtx* dctx = new DecompressCtx();

        #pragma omp for schedule(static)
        for (uint i = 0; i < entries.size(); i++) {
            CSVEntry* entry = &entries[i];
            if (entry->dtm_str != "") {
                EGPosition pos;
                pos.from_fen(entry->fen);
                std::ostringstream mate_line;
                for (Move move : get_mate_line(pos, dctx)) {
                    mate_line << move_to_uci(move) << " " ;
                }
                entry->mate_line = mate_line.str();
            }
        }

        #pragma omp critical
        {
            probe_count += dctx->probe_count;
            delete dctx;
        }
    }

    TimePoint t1 = now();
    std::cout << "Finished in " << (double) (t1-t0) / 1000 << "s. Probe count = " << probe_count << " (" << (double) (t1-t0) / probe_count << "ms/probe)" << std::endl;


    std::ofstream ofile("longest_mate_lines.csv");
    ofile << "id,numpos,bytes,fen,dtm,line\n";

    for (uint i = 0; i < entries.size(); i++) {
        CSVEntry entry = entries[i];
        ofile << entry.egtb_id << "," << entry.num_pos_str << "," << entry.bytes_str << "," << entry.fen << "," << entry.dtm_str << "," << entry.mate_line << "\n";
    }

    ofile.close();
}
