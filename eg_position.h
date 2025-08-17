
#ifndef EG_POSITION_H_INCLUDED
#define EG_POSITION_H_INCLUDED

#include "types.h"
#include "bitboard.h"
using namespace Stockfish;

// no castling
class EGPosition {
    public:
    void put_piece(Piece pc, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);

    // Position representation
    Bitboard pieces() const;  // All pieces
    template<typename... PieceTypes>
    Bitboard pieces(PieceTypes... pts) const;
    Bitboard pieces(Color c) const;
    template<typename... PieceTypes>
    Bitboard pieces(Color c, PieceTypes... pts) const;
    Piece    piece_on(Square s) const;

    template<PieceType Pt>
    int count(Color c) const;
    template<PieceType Pt>
    int count() const;
    template<PieceType Pt>
    Square square(Color c) const;

    Color side_to_move() const;

    Bitboard attackers_to(Square s) const;
    Bitboard attackers_to(Square s, Bitboard occupied) const;
    bool attackers_to_exist(Square s, Bitboard occupied, Color c) const;

    Bitboard blockers_for_king(Color c) const;

    Bitboard checkers(Color c) const;
    bool has_evasions(Color c, Bitboard checkersBB) const;

    bool is_legal() const;
    bool is_legal_checkmate() const;

    private:
        Piece      board[SQUARE_NB];
        Bitboard   byTypeBB[PIECE_TYPE_NB];
        Bitboard   byColorBB[COLOR_NB];
        Color      sideToMove;

        bool has_king_evasions(Color c) const;
        template<PieceType Pt>
        bool has_piece_evasions(Color c, Bitboard pinned, Bitboard target) const;
        template<Color c>
        bool has_pawn_evasions(Square checkerSq, Bitboard pinned, Bitboard block) const;
};

inline void EGPosition::put_piece(Piece pc, Square s) {
    board[s] = pc;
    byTypeBB[ALL_PIECES] |= byTypeBB[type_of(pc)] |= s;
    byColorBB[color_of(pc)] |= s;
}

inline void EGPosition::remove_piece(Square s) {
    Piece pc = board[s];
    byTypeBB[ALL_PIECES] ^= s;
    byTypeBB[type_of(pc)] ^= s;
    byColorBB[color_of(pc)] ^= s;
    board[s] = NO_PIECE;
}

inline void EGPosition::move_piece(Square from, Square to) {
    Piece    pc     = board[from];
    Bitboard fromTo = from | to;
    byTypeBB[ALL_PIECES] ^= fromTo;
    byTypeBB[type_of(pc)] ^= fromTo;
    byColorBB[color_of(pc)] ^= fromTo;
    board[from] = NO_PIECE;
    board[to]   = pc;
}

inline Color EGPosition::side_to_move() const { return sideToMove; }

inline Piece EGPosition::piece_on(Square s) const {
    assert(is_ok(s));
    return board[s];
}

inline Bitboard EGPosition::pieces() const { return byTypeBB[ALL_PIECES]; }

template<typename... PieceTypes>
inline Bitboard EGPosition::pieces(PieceTypes... pts) const {
    return (byTypeBB[pts] | ...);
}

inline Bitboard EGPosition::pieces(Color c) const { return byColorBB[c]; }

template<typename... PieceTypes>
inline Bitboard EGPosition::pieces(Color c, PieceTypes... pts) const {
    return pieces(c) & pieces(pts...);
}

template<PieceType Pt>
inline int EGPosition::count(Color c) const {
    return pieceCount[make_piece(c, Pt)];
}

template<PieceType Pt>
inline int EGPosition::count() const {
    return count<Pt>(WHITE) + count<Pt>(BLACK);
}

template<PieceType Pt>
inline Square EGPosition::square(Color c) const {
    assert(count<Pt>(c) == 1);
    return lsb(pieces(c, Pt));
}
inline Bitboard EGPosition::attackers_to(Square s) const { return attackers_to(s, pieces()); }

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

// checkers to king of color c
inline Bitboard EGPosition::checkers(Color c) const {
    return attackers_to(square<KING>(c)) & pieces(~c);
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

template<PieceType Pt>
bool EGPosition::has_piece_evasions(Color c, Bitboard pinned, Bitboard target) const {
    // can capture checker (in target) or block check from slider (move to target) with non-pinned piece?
    Bitboard bb = pieces(c, Pt) ^ pinned;
    while (bb) {
        Square   from = pop_lsb(bb);
        if (attacks_bb<Pt>(from, pieces()) & target) {
            return true;
        }
    }
    return false;
}

template<Color c>
bool EGPosition::has_pawn_evasions(Square checkerSq, Bitboard pinned, Bitboard block) const {
    constexpr Direction Up       = (c == WHITE ? NORTH : SOUTH);
    constexpr Direction UpRight  = (c == WHITE ? NORTH_EAST : SOUTH_WEST);
    constexpr Direction UpLeft   = (c == WHITE ? NORTH_WEST : SOUTH_EAST);
    constexpr Bitboard  TRank3BB = (c == WHITE ? Rank3BB : Rank6BB);
    const Bitboard emptySquares = ~pieces();


    Bitboard unpinned_pawns = pieces(c, PAWN) ^ pinned;
    Bitboard pawn_captures = shift<UpLeft>(unpinned_pawns) | shift<UpRight>(unpinned_pawns);
    if (pawn_captures & checkerSq) {
        return true; // can capture checker (this also handles capture promotion)
        // could also check reverse pawnattacks from checkerSq & unpinned_pawns
    }
    Bitboard pawn_pushes = shift<Up>(unpinned_pawns) & emptySquares; // this also handles blocking with promotion
    pawn_pushes |= (shift<Up>(pawn_pushes & TRank3BB) & emptySquares); // block with double pawn push
    if (pawn_pushes & block) {
        return true;
    }

    // TODO: handle en-passant

    return false;
}

bool EGPosition::has_evasions(Color c, Bitboard checkersBB) const {
    if (more_than_one(checkersBB)) {
        return has_king_evasions(c);
    } else {
        if (has_king_evasions(c)) { return true; }

        const Square ksq = square<KING>(c);
        const Square checkerSq = lsb(checkersBB);
        const Bitboard target = between_bb(ksq, checkerSq); // checkerSq in target if squares are not on same rank/file/diagonal
        const Bitboard pinned = blockers_for_king(c) & pieces(c);

        if (has_piece_evasions<KNIGHT>(c, pinned, target)) { return true; }
        if (has_piece_evasions<BISHOP>(c, pinned, target)) { return true; }
        if (has_piece_evasions<ROOK>(c, pinned, target)) { return true; }
        if (has_piece_evasions<QUEEN>(c, pinned, target)) { return true; }
        if (c == WHITE) {
            if (has_pawn_evasions<WHITE>(checkerSq, pinned, target)) { return true; }
        } else {
            if (has_pawn_evasions<BLACK>(checkerSq, pinned, target)) { return true; }
        }
        return false;
    }
}

// checks if side not to move is not in check
bool EGPosition::is_legal() const {
    Color c = side_to_move();
    return checkers(~c) == 0;
}

// checks if king of color c = side_to_move is in checkmate and king of color ~c is not in check
bool EGPosition::is_legal_checkmate() const {
    if (!is_legal()) { return false;}
    Color c = side_to_move();
    Bitboard checkersBB = checkers(c);
    return !has_evasions(c, checkersBB);
}

#endif