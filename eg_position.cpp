
#include "eg_position.h"

bool EGPosition::is_equal(const EGPosition& pos) const {
    if (sideToMove != pos.sideToMove) { return false; }
    if (epSquare != pos.epSquare) { return false; }
    for (int i = 0; i < SQUARE_NB; i++) {
        if (board[i] != pos.board[i]) { return false; }
    }
    for (int i = 0; i < PIECE_TYPE_NB; i++) {
        if (byTypeBB[i] != pos.byTypeBB[i]) { return false; }
    }
    for (int i = 0; i < COLOR_NB; i++) {
        if (byColorBB[i] != pos.byColorBB[i]) { return false; }
    }
    return true;
}

// Returns an ASCII representation of the position
std::ostream& operator<<(std::ostream& os, const EGPosition& pos) {

    os << " +---+---+---+---+---+---+---+---+\n";

    for (Rank r = RANK_8; r >= RANK_1; --r)
    {
        for (File f = FILE_A; f <= FILE_H; ++f)
            os << " | " << PieceToChar[pos.piece_on(make_square(f, r))];

        os << " | " << (1 + r) << "\n +---+---+---+---+---+---+---+---+\n";
    }

    os << "   a   b   c   d   e   f   g   h";// << "Checkers: ";
    os << "    STM: " << (pos.side_to_move() == WHITE ? "WHITE" : "BLACK");
    os << (pos.ep_square() == SQ_NONE ? " - " : ", ep: " + square_to_uci(pos.ep_square()) + " ");
    os << "\n";
    // os << Bitboards::pretty(pos.pieces()) << "\n";
    // for (Bitboard b = pos.checkers(pos.side_to_move()); b;)
    //     os << square_to_uci(pop_lsb(b)) << " ";

    return os;
}

std::string EGPosition::fen() const {

    int                emptyCnt;
    std::ostringstream ss;

    for (Rank r = RANK_8; r >= RANK_1; --r)
    {
        for (File f = FILE_A; f <= FILE_H; ++f)
        {
            for (emptyCnt = 0; f <= FILE_H && empty(make_square(f, r)); ++f)
                ++emptyCnt;

            if (emptyCnt)
                ss << emptyCnt;

            if (f <= FILE_H)
                ss << PieceToChar[piece_on(make_square(f, r))];
        }

        if (r > RANK_1)
            ss << '/';
    }

    ss << (sideToMove == WHITE ? " w " : " b ");

    ss << '-';

    ss << (ep_square() == SQ_NONE ? " - " : " " + square_to_uci(ep_square()) + " ");
    return ss.str();
}
void EGPosition::from_fen(std::string fenStr) {
    reset();

    unsigned char      col, row, token;
    size_t             idx;
    Square             sq = SQ_A8;
    std::istringstream ss(fenStr);

    ss >> std::noskipws;

    // 1. Piece placement
    while ((ss >> token) && !isspace(token)) {
        if (isdigit(token))
            sq += (token - '0') * EAST;  // Advance the given number of files

        else if (token == '/')
            sq += 2 * SOUTH;

        else if ((idx = PieceToChar.find(token)) != std::string::npos)
        {
            put_piece(Piece(idx), sq);
            ++sq;
        }
    }

    // 2. Active color
    ss >> token;
    sideToMove = (token == 'w' ? WHITE : BLACK);
    ss >> token;

    // 3. Not castling. 
    while ((ss >> token) && !isspace(token)) {
        token = char(toupper(token));
        assert(token != 'K' && token != 'Q');

    }

    // 4. En passant square.
    // Ignore if square is invalid or not on side to move relative rank 6.
    epSquare = SQ_NONE;
    if (((ss >> col) && (col >= 'a' && col <= 'h'))
        && ((ss >> row) && (row == (sideToMove == WHITE ? '6' : '3'))))
    {
        epSquare = make_square(File(col - 'a'), Rank(row - '1'));
        assert(check_ep(epSquare)); // only accept legal ep squares
    }
}

bool EGPosition::check_ep(Square ep_sq) const {
    Color  us       = ~sideToMove;
    Color  them     = ~us;
    Square from = ep_sq - pawn_push(us);
    Square to = ep_sq + pawn_push(us);

    if (piece_on(to) != make_piece(us, PAWN)) {
        // there has to be pawn that can be captured with ep
        return false;
    }
    
    if (piece_on(ep_sq) || piece_on(from)) {
        // ep_sq and from have to be empty since pawn should have been able to make double push
        return false;
    }

    if (!((us == WHITE && (from & Rank2BB)) || (us == BLACK && (from & Rank7BB)))) {
        // origin square of pawn (from) has to be on the start rank of pawns
        return false;
    }


    Bitboard pawns = attacks_bb<PAWN>(ep_sq, us) & pieces(them, PAWN);
    if (!pawns)
        // there are no pawns attacking ep sq
        return false;

    if (checkers(us) & ~square_bb(to))
        // we are in check, ep is only possible if pawn to be captured with ep gives check
        return false;

    if (more_than_one(pawns)) {
        // there are two pawns available for ep
        if (!more_than_one(blockers_for_king(them) & pawns)) {
            return true;
        }
        if (!(file_bb(square<KING>(them)) & pawns))
            return false;

        pawns &= ~file_bb(square<KING>(them));
    }

    Square   ksq      = square<KING>(them);
    Square   capsq    = to;
    Bitboard occupied = (pieces() ^ lsb(pawns) ^ capsq) | (ep_sq);

    // If our king is not attacked after making the move, ep is legal.
    if (!(attacks_bb<ROOK>(ksq, occupied) & pieces(us, QUEEN, ROOK))
        && !(attacks_bb<BISHOP>(ksq, occupied) & pieces(us, QUEEN, BISHOP)))
        return true;

    return false;
}


UndoInfo EGPosition::do_move(Move m) {
    Square from     = m.from_sq();
    Square to       = m.to_sq();
    Piece  pc       = piece_on(from);
    Color  us       = sideToMove;
    Color  them     = ~us;
    Piece  captured = m.type_of() == EN_PASSANT ? make_piece(them, PAWN) : piece_on(to);
    Square old_epSquare = epSquare;
    epSquare = SQ_NONE;
    if (captured) {
        Square capsq = (m.type_of() == EN_PASSANT) ? to - pawn_push(us) : to;
        remove_piece(capsq);
    }
    move_piece(from, to);
    if (m.type_of() == PROMOTION) {
        remove_piece(to);
        put_piece(make_piece(us,m.promotion_type()), to);
    }

    sideToMove = ~sideToMove;

    if (type_of(pc) == PAWN){
        bool checkEP = ((int(to) ^ int(from)) == 16);
        if (checkEP && check_ep(to - pawn_push(us))) {
            epSquare = to - pawn_push(us);
        }
    }


    return UndoInfo(type_of(captured), old_epSquare);
}

void EGPosition::undo_move(Move m, UndoInfo u) {
    Square from     = m.from_sq();
    Square to       = m.to_sq();

    sideToMove = ~sideToMove;

    Color  us       = sideToMove;

    if (m.type_of() == PROMOTION) {
        remove_piece(to);
        put_piece(make_piece(us,PAWN), to);
    }
    move_piece(to, from);
    epSquare = u.epSquare;
    if (u.captured) {
        Square capsq = to;
        if (m.type_of() == EN_PASSANT) {
            capsq -= pawn_push(us);
        }
        put_piece(make_piece(~us, u.captured), capsq);
    }
}

// Computes a bitboard of all pieces which attack a given square.
// Slider attacks use the occupied bitboard to indicate occupancy.
Bitboard EGPosition::attackers_to(Square s, Bitboard occupied) const {
    return (attacks_bb<ROOK>(s, occupied) & pieces(ROOK, QUEEN))
         | (attacks_bb<BISHOP>(s, occupied) & pieces(BISHOP, QUEEN))
         | (attacks_bb<PAWN>(s, BLACK) & pieces(WHITE, PAWN))
         | (attacks_bb<PAWN>(s, WHITE) & pieces(BLACK, PAWN))
         | (attacks_bb<KNIGHT>(s) & pieces(KNIGHT)) | (attacks_bb<KING>(s) & pieces(KING));
}

bool EGPosition::attackers_to_exist(Square s, Bitboard occupied, Color c) const {
    return ((attacks_bb<ROOK>(s) & pieces(c, ROOK, QUEEN))
            && (attacks_bb<ROOK>(s, occupied) & pieces(c, ROOK, QUEEN)))
        || ((attacks_bb<BISHOP>(s) & pieces(c, BISHOP, QUEEN))
            && (attacks_bb<BISHOP>(s, occupied) & pieces(c, BISHOP, QUEEN)))
        || (((attacks_bb<PAWN>(s, ~c) & pieces(PAWN)) | (attacks_bb<KNIGHT>(s) & pieces(KNIGHT))
             | (attacks_bb<KING>(s) & pieces(KING)))
            & pieces(c));
}

bool EGPosition::has_king_evasions(Color c) const {
    const Square ksq = square<KING>(c);
    Bitboard b = attacks_bb<KING>(ksq) & ~pieces(c);
    while (b) {
        Square to = pop_lsb(b);
        if (!(attackers_to_exist(to, pieces() ^ ksq, ~c))) {
            return true;
        }
    }
    return false;
}

// pieces preventing king of color c from being in check
Bitboard EGPosition::blockers_for_king(Color c) const {
    Square ksq = square<KING>(c);
    Bitboard blockersForKing = 0;

    Bitboard snipers = ((attacks_bb<ROOK>(ksq) & pieces(QUEEN, ROOK))
                        | (attacks_bb<BISHOP>(ksq) & pieces(QUEEN, BISHOP)))
                     & pieces(~c);
    Bitboard occupancy = pieces() ^ snipers;

    while (snipers) {
        Square   sniperSq = pop_lsb(snipers);
        Bitboard b        = between_bb(ksq, sniperSq) & occupancy;
        if (b && !more_than_one(b)) {
            blockersForKing |= b;
        }
    }
    return blockersForKing;
}