#include <iostream>
#include <vector>
#include "bitboard.h"
#include "values.h"
#include "kkx.h"
#include "misc.h"
#include "triangular_indexes.h"
#include "egtb.h"
#include "eg_movegen.h"
#include <fstream>
#include <unordered_map>
#include <omp.h>

std::unordered_map<std::string, EGTB*> id_to_egtb = {};


std::string egtb_id_from_pos(const EGPosition pos) {
    std::ostringstream os;
    for (Color c : {pos.side_to_move(), ~pos.side_to_move()}) {
        for (PieceType pt : {KING, QUEEN, ROOK, BISHOP, KNIGHT, PAWN}) {
            for (int i = 0; i < pos.count(c, pt); i++) {
                os << PieceToChar[pt];
            }
        }
    }
    return os.str();
}

std::string get_mirror_id(const std::string& egtb_id) {
    size_t firstK = egtb_id.find('K');
    size_t secondK = egtb_id.find('K', firstK + 1);
    std::string stuff1 = egtb_id.substr(firstK + 1, secondK - firstK - 1);
    std::string stuff2 = egtb_id.substr(secondK + 1);
    return "K" + stuff2 + "K" + stuff1;
}

uint64_t query_count = 0;
int16_t query_position(EGPosition pos, DecompressCtx* dctx) {
    std::string egtb_id = egtb_id_from_pos(pos);

    if (id_to_egtb[egtb_id] != nullptr) {
        #pragma omp atomic
        query_count++;
        return id_to_egtb[egtb_id]->query_postion_dctx(pos, dctx);
    } else {
        EGMoveList movelist = EGMoveList<FORWARD>(pos);
        if (movelist.size() == 0) {
            if (pos.stm_in_check()) {
                return LOSS_IN(0);
            } else {
                return 0;
            }
        }
        int16_t max_val = LOSS_IN(0);
        for (Move move : movelist) {
            UndoInfo u = pos.do_move(move);
            int16_t val = query_position(pos, dctx);
            max_val = std::max(max_val, (int16_t) -val);
            pos.undo_move(move, u);
        }
        if (max_val > 0) max_val--;
        if (max_val < 0) max_val++;
        return max_val;
    }
}


std::vector<Move> get_pv(EGPosition pos, DecompressCtx* dctx) {
    int16_t val = query_position(pos, dctx);
    std::vector<Move> pv;
    while (val != LOSS_IN(0)) {
        // std::cout << val << std::endl;
        val = -val;
        if (val > 0) val++;
        if (val < 0) val--;
        bool found = false;
        for (Move move : EGMoveList<FORWARD>(pos)) {
            UndoInfo u = pos.do_move(move);
            int16_t move_val = query_position(pos, dctx);
            // std::cout << move_to_uci(move) << " " << move_val << std::endl;
            pos.undo_move(move, u);
            if (move_val == val) {
                pv.push_back(move);
                pos.do_move(move);
                // std::cout << move_to_uci(move) << std::endl;
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
    std::string fen;
    std::string ply_str;
};

#define LOAD_HALF 1

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

    for (std::string egtb_id : egtb_ids) {
        id_to_egtb[egtb_id] = new EGTB(egtb_id);
        id_to_egtb[egtb_id]->init_compressed_tb(folder);
    }

    
#if LOAD_HALF
    for (std::string egtb_id : egtb_ids) {
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
#endif
    
    uint64_t count = 0;
    uint64_t compressed_filesize = 0;
    for (std::string egtb_id : egtb_ids) {
        if (id_to_egtb[egtb_id] != nullptr) {
            count++;
            compressed_filesize += id_to_egtb[egtb_id]->CTB->compressed_filesize;
        }
    }
    std::cout << "Loaded " << count << " EGTBs (" << compressed_filesize / (1024*1024*1024) << "GiB)" << std::endl;

    std::ifstream file("longest_mates.csv");std::string line;

    std::vector<CSVEntry> entries;
    std::getline(file, line); // header
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string egtb_id, fen, ply_str;

        std::getline(ss, egtb_id, ',');
        std::getline(ss, fen, ',');
        std::getline(ss, ply_str, ',');

        entries.push_back({egtb_id, fen, ply_str});
    }

    TimePoint t0 = now();
    #pragma omp parallel num_threads(nthreads)
    {
        DecompressCtx* dctx = new DecompressCtx();

        #pragma omp for schedule(static)
        for (uint i = 0; i < entries.size(); i++) {
            CSVEntry entry = entries[i];
            // std::cout << i << ". ID: " << entry.egtb_id << " FEN: " << entry.fen << " Plies: " << entry.ply_str << std::endl;
            if (entry.ply_str != "") {
                EGPosition pos;
                pos.from_fen(entry.fen);
                get_pv(pos, dctx);
                // for (Move move : get_pv(pos)) {
                //     std::cout << " " << move_to_uci(move) ;
                // }
                // std::cout << std::endl;
            }
        }

        #pragma omp critical
        {
            delete dctx;
        }
    }

    TimePoint t1 = now();
    std::cout << "Finished in " << (double) (t1-t0) / 1000 << "s. Query count = " << query_count << " (" << (double) (t1-t0) / query_count << "ms/query)" << std::endl;

}

// Finished in 312.912s. Query count =   757364 (0.413159ms/query)
// Finished in 6675.26s. Query count = 15767444 (0.423357ms/query) // double count
// Finished in 441.334s. Query count = 7883722 (0.0559804ms/query) // multi threaded