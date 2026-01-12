
#ifndef EG_POSITION_H_INCLUDED
#define EG_POSITION_H_INCLUDED

#include <iostream>
#include <cstring>
#include <iomanip>

#include "types.h"
#include "bitboard.h"
#include "uci.h"


struct UndoInfo {
    Move move;
    PieceType captured;
    Square epSquare;
    UndoInfo() = default;
    UndoInfo(Move move_, PieceType captured_, Square epSquare_){
        this->move = move_;
        this->captured = captured_;
        this->epSquare = epSquare_;
    }
};

// no castling
class EGPosition {
public:
    EGPosition() = default;


    void put_piece(Piece pc, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
    void set_side_to_move(Color c);
    void flip_side_to_move();

    // Position representation
    Bitboard pieces() const;  // All pieces
    template<typename... PieceTypes>
    Bitboard pieces(PieceTypes... pts) const;
    Bitboard pieces(Color c) const;
    template<typename... PieceTypes>
    Bitboard pieces(Color c, PieceTypes... pts) const;
    Piece    piece_on(Square s) const;
    bool empty(Square s) const;

    template<PieceType Pt>
    Square square(Color c) const;

    Color side_to_move() const;

    Bitboard attackers_to(Square s) const;
    Bitboard attackers_to(Square s, Bitboard occupied) const;
    bool attackers_to_exist(Square s, Bitboard occupied, Color c) const;

    Bitboard blockers_for_king(Color c) const;

    Bitboard checkers(Color c) const;

    bool sntm_in_check() const;
    bool stm_in_check() const;

    bool is_equal(const EGPosition& pos) const;

    std::string fen() const;
    void from_fen(std::string fenStr);

    UndoInfo do_move(Move m);
    void undo_move(UndoInfo u);

    void do_rev_move(UndoInfo u);
    void undo_rev_move(UndoInfo u);

    // int& get_wpiece_count() const;
    // int* get_bpiece_count() const;

    template<PieceType Pt>
    int count(Color c) const;

    int count(Color c, PieceType pt) const;

    void reset();

    Square ep_square() const;
    void set_ep_square(Square sq);
    bool check_ep(Square ep_sq) const;
    Bitboard ep_candidates() const;


private:
        Piece      board[SQUARE_NB];
        Bitboard   byTypeBB[PIECE_TYPE_NB];
        Bitboard   byColorBB[COLOR_NB];
        Color      sideToMove;
        int        pieceCount[PIECE_NB];
        Square     epSquare;

        bool has_king_evasions(Color c) const;
        template<PieceType Pt>
        bool has_piece_evasions(Color c, Bitboard pinned, Bitboard target) const;
        template<Color c>
        bool has_pawn_evasions(Square checkerSq, Bitboard pinned, Bitboard block) const;
};

template<PieceType Pt>
inline int EGPosition::count(Color c) const {
    return pieceCount[make_piece(c, Pt)];
}

inline int EGPosition::count(Color c, PieceType pt) const {
    return pieceCount[make_piece(c, pt)];
}

// Returns an ASCII representation of the position
std::ostream& operator<<(std::ostream& os, const EGPosition& pos);

inline void EGPosition::reset() {
    std::memset(this, 0, sizeof(EGPosition));
    epSquare = SQ_NONE;
}


inline void EGPosition::put_piece(Piece pc, Square s) {
    board[s] = pc;
    byTypeBB[ALL_PIECES] |= byTypeBB[type_of(pc)] |= s;
    byColorBB[color_of(pc)] |= s;
    pieceCount[pc]++;
    pieceCount[make_piece(color_of(pc), ALL_PIECES)]++;
}

inline void EGPosition::remove_piece(Square s) {
    Piece pc = board[s];
    byTypeBB[ALL_PIECES] ^= s;
    byTypeBB[type_of(pc)] ^= s;
    byColorBB[color_of(pc)] ^= s;
    board[s] = NO_PIECE;
    pieceCount[pc]--;
    pieceCount[make_piece(color_of(pc), ALL_PIECES)]--;
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

inline Bitboard EGPosition::ep_candidates() const {
    if (sideToMove == WHITE)
        return shift<NORTH>(pieces(BLACK, PAWN) & Rank5BB) & ~pieces();
    else
        return shift<SOUTH>(pieces(WHITE, PAWN) & Rank4BB) & ~pieces();
}

inline void EGPosition::do_rev_move(UndoInfo u) {
    undo_move(u);
}

inline void EGPosition::undo_rev_move(UndoInfo u) {
    do_move(u.move);
}

inline Color EGPosition::side_to_move() const { return sideToMove; }

inline Square EGPosition::ep_square() const { return epSquare; }
inline void EGPosition::set_ep_square(Square sq) { epSquare = sq; }

inline void EGPosition::set_side_to_move(Color c) { sideToMove = c; }

inline void EGPosition::flip_side_to_move() { sideToMove = ~sideToMove; }

inline Piece EGPosition::piece_on(Square s) const {
    assert(is_ok(s));
    return board[s];
}
inline bool EGPosition::empty(Square s) const { return piece_on(s) == NO_PIECE; }


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
inline Square EGPosition::square(Color c) const {
    return lsb(pieces(c, Pt));
}
inline Bitboard EGPosition::attackers_to(Square s) const { return attackers_to(s, pieces()); }

// checkers to king of color c
inline Bitboard EGPosition::checkers(Color c) const {
    return attackers_to(square<KING>(c)) & pieces(~c);
}

inline bool EGPosition::sntm_in_check() const {
    return checkers(~side_to_move());
}

inline bool EGPosition::stm_in_check() const {
    return checkers(side_to_move());
}


#endif