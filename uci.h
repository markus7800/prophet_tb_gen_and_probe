#ifndef UCI_H_INCLUDED
#define UCI_H_INCLUDED

#include "types.h"
#include "position.h"

std::string square_to_uci(Square s);

std::string move_to_uci(Move m, bool chess960);


#endif