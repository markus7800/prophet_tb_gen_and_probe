#include "uci.h"

namespace Prophet {

std::string square_to_uci(Square s) {
    return std::string{char('a' + file_of(s)), char('1' + rank_of(s))};
}

std::string move_to_uci(Move m) {
    if (m == Move::none())
        return "(none)";

    if (m == Move::null())
        return "0000";

    Square from = m.from_sq();
    Square to   = m.to_sq();

    if (m.type_of() == CASTLING)
        to = make_square(to > from ? FILE_G : FILE_C, rank_of(from));

    std::string move = square_to_uci(from) + square_to_uci(to);

    if (m.type_of() == PROMOTION)
        move += " pnbrqk"[m.promotion_type()];

    return move;
}

} // namespace Prophet
