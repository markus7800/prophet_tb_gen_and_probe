#include <iostream>
#include <vector>
#include "bitboard.h"
#include "values.h"
#include "kkx.h"
#include "triangular_indexes.h"
#include "egtb_iterator.h"
#include "eg_movegen.h"
#include <fstream>
#include <unordered_map>

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
int16_t query_position(const EGPosition pos) {
    return id_to_egtb[egtb_id_from_pos(pos)]->query_postion(pos);
}


std::vector<Move> get_pv(EGPosition pos) {
    int16_t val = query_position(pos);
    std::vector<Move> pv;
    while (val != LOSS_IN(0)) {
        // std::cout << val << std::endl;
        val = -val;
        if (val > 0) val++;
        if (val < 0) val--;
        bool found = false;
        for (Move move : EGMoveList<FORWARD>(pos)) {
            UndoInfo u = pos.do_move(move);
            int16_t move_val = query_position(pos);
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

int main() {
    Bitboards::init();
    init_kkx_table();
    init_kkp_table();
    init_tril();

    std::string folder = "egtbs";
    std::vector<std::string> egtb_ids = get_egtb_identifiers(0, 4);

    for (std::string egtb_id : egtb_ids) {
        id_to_egtb[egtb_id] = new EGTB(egtb_id);
        id_to_egtb[egtb_id]->init_compressed_tb(folder);
    }
    std::cout << "Loaded " << egtb_ids.size() << " EGTBs" << std::endl;

    std::ifstream file("longest_mates.csv");std::string line;

    std::getline(file, line); // header
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string egtb_id, fen, pliesStr;

        std::getline(ss, egtb_id, ',');
        std::getline(ss, fen, ',');
        std::getline(ss, pliesStr, ',');

        std::cout << "ID: " << egtb_id << " FEN: " << fen << " Plies: " << pliesStr << std::endl;

        if (pliesStr != "") {
            EGPosition pos;
            pos.from_fen(fen);
            for (Move move : get_pv(pos)) {
                std::cout << " " << move_to_uci(move) ;
            }
            std::cout << std::endl;
        }
    }

}