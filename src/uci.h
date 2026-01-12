#ifndef UCI_H_INCLUDED
#define UCI_H_INCLUDED

#include "types.h"

#include <iostream>

constexpr std::string_view PieceToChar(" PNBRQK  pnbrqk");

std::string square_to_uci(Square s);

std::string move_to_uci(Move m);

#endif