#ifndef EGTB_ITERATOR_H_INCLUDED
#define EGTB_ITERATOR_H_INCLUDED

#include <vector>
#include <unordered_set>
#include <string>
#include <unordered_map>
#include "types.h"
#include <sstream>
#include "uci.h"

namespace Prophet {

std::string get_pieces_identifier(int pieces[6]);

std::string get_egtb_identifier(int stm_pieces[6], int sntm_pieces[6]);

void egtb_id_to_pieces(std::string egtb_id, int pieces1[6], int pieces2[6]);

std::string get_mirror_id(const std::string& egtb_id);

void place_piece(Piece p, int* pieces1, int* pieces2);
void unplace_piece(Piece p, int* pieces1, int* pieces2);

std::vector<std::string> get_egtb_identifiers(int MIN_PIECE_COUNT = 0, int MAX_PIECE_COUNT = 4, int MIN_PAWN_COUNT = 0, int MAX_PAWN_COUNT = 4);


extern std::unordered_map<std::string, int> EGTB_ID_TO_IX;
void init_egtb_id_to_ix();

} // namespace Prophet

#endif
