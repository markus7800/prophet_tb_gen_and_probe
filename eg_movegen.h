
#ifndef EG_MOVEGEN_H_INCLUDED
#define EG_MOVEGEN_H_INCLUDED

#include "eg_position.h"

enum EGGenType {
    FORWARD, // all legal moves except castling and en-passant (for now)
    FWD_EVASIONS, // if in check all legal evasions
    FWD_NON_EVASIONS,
    REVERSE, // all non-capturing backward moves (no castling, no en-passant)
    REV_CHECKS,
    REV_NONCHECKS
};


template<Direction offset>
inline Move* splat_pawn_moves(Move* moveList, Bitboard to_bb) {
    while (to_bb)
    {
        Square to   = pop_lsb(to_bb);
        *moveList++ = Move(to - offset, to);
    }
    return moveList;
}

inline Move* splat_moves(Move* moveList, Square from, Bitboard to_bb) {
    while (to_bb)
        *moveList++ = Move(from, pop_lsb(to_bb));
    return moveList;
}

template<EGGenType Type, Direction D>
Move* make_promotions(Move* moveList, Square to) {
    *moveList++ = Move::make<PROMOTION>(to - D, to, QUEEN);
    *moveList++ = Move::make<PROMOTION>(to - D, to, ROOK);
    *moveList++ = Move::make<PROMOTION>(to - D, to, BISHOP);
    *moveList++ = Move::make<PROMOTION>(to - D, to, KNIGHT);
    return moveList;
}

template<Color Us,EGGenType Type>
Move* generate_pawn_moves(const EGPosition& pos, Move* moveList, Bitboard target, Bitboard checkers) {

    constexpr Color     Them     = ~Us;
    constexpr Bitboard  TRank7BB = (Us == WHITE ? Rank7BB : Rank2BB);
    constexpr Bitboard  TRank3BB = (Us == WHITE ? Rank3BB : Rank6BB);
    constexpr Direction Up       = (Us == WHITE ? NORTH : SOUTH);
    constexpr Direction UpRight  = (Us == WHITE ? NORTH_EAST : SOUTH_WEST);
    constexpr Direction UpLeft   = (Us == WHITE ? NORTH_WEST : SOUTH_EAST);

    const Bitboard emptySquares = ~pos.pieces();
    const Bitboard enemies      = Type == FWD_EVASIONS ? checkers : pos.pieces(Them);

    Bitboard pawnsOn7    = pos.pieces(Us, PAWN) & TRank7BB;
    Bitboard pawnsNotOn7 = pos.pieces(Us, PAWN) & ~TRank7BB;

    // Single and double pawn pushes, no promotions
    {
        Bitboard b1 = shift<Up>(pawnsNotOn7) & emptySquares;
        Bitboard b2 = shift<Up>(b1 & TRank3BB) & emptySquares;

        if constexpr (Type == FWD_EVASIONS)  // Consider only blocking squares
        {
            b1 &= target;
            b2 &= target;
        }

        moveList = splat_pawn_moves<Up>(moveList, b1);
        moveList = splat_pawn_moves<Up + Up>(moveList, b2);
    }

    // Promotions and underpromotions
    if (pawnsOn7)
    {
        Bitboard b1 = shift<UpRight>(pawnsOn7) & enemies;
        Bitboard b2 = shift<UpLeft>(pawnsOn7) & enemies;
        Bitboard b3 = shift<Up>(pawnsOn7) & emptySquares;

        if constexpr (Type == FWD_EVASIONS)
            b3 &= target;

        while (b1)
            moveList = make_promotions<Type, UpRight>(moveList, pop_lsb(b1));

        while (b2)
            moveList = make_promotions<Type, UpLeft>(moveList, pop_lsb(b2));

        while (b3)
            moveList = make_promotions<Type, Up>(moveList, pop_lsb(b3));
    }
   
    // Standard and en passant captures
    {
        Bitboard b1 = shift<UpRight>(pawnsNotOn7) & enemies;
        Bitboard b2 = shift<UpLeft>(pawnsNotOn7) & enemies;

        moveList = splat_pawn_moves<UpRight>(moveList, b1);
        moveList = splat_pawn_moves<UpLeft>(moveList, b2);

        // TODO: en-passant
    }

    return moveList;
}


template<Color Us, PieceType Pt>
Move* generate_moves(const EGPosition& pos, Move* moveList, Bitboard target) {

    static_assert(Pt != KING && Pt != PAWN, "Unsupported piece type in generate_moves()");

    Bitboard bb = pos.pieces(Us, Pt);

    while (bb)
    {
        Square   from = pop_lsb(bb);
        Bitboard b    = attacks_bb<Pt>(from, pos.pieces()) & target;

        moveList = splat_moves(moveList, from, b);
    }

    return moveList;
}

template<Color Us, EGGenType Type>
Move* generate_all_fwd(const EGPosition& pos, Move* moveList, Bitboard checkers) {
    static_assert(Type == FWD_EVASIONS || Type == FWD_NON_EVASIONS);

    const Square ksq = pos.square<KING>(Us);
    Bitboard     target;

    // Skip generating non-king moves when in double check
    if (Type != FWD_EVASIONS || !more_than_one(checkers)) {
        target = Type == FWD_EVASIONS ? between_bb(ksq, lsb(checkers)): pos.pieces(Us);

        moveList = generate_pawn_moves<Us, Type>(pos, moveList, target, checkers);
        moveList = generate_moves<Us, KNIGHT>(pos, moveList, target);
        moveList = generate_moves<Us, BISHOP>(pos, moveList, target);
        moveList = generate_moves<Us, ROOK>(pos, moveList, target);
        moveList = generate_moves<Us, QUEEN>(pos, moveList, target);
    }

    Bitboard b = attacks_bb<KING>(ksq) & (Type == FWD_EVASIONS ? ~pos.pieces(Us) : target);
    moveList = splat_moves(moveList, ksq, b);

    return moveList;
}

template<EGGenType>
Move* generate(const EGPosition& pos, Move* moveList);

template<>
Move* generate<FORWARD>(const EGPosition& pos, Move* moveList) {

    Color    us     = pos.side_to_move();
    Bitboard pinned = pos.blockers_for_king(us) & pos.pieces(us);
    Square   ksq    = pos.square<KING>(us);
    Move*    cur    = moveList;
    Bitboard checkers = pos.checkers(us);

    if (us == WHITE)
        moveList = checkers ? generate_all_fwd<WHITE,FWD_EVASIONS>(pos, moveList, checkers) : generate_all_fwd<WHITE,FWD_NON_EVASIONS>(pos, moveList, checkers);
    else
        moveList = checkers ? generate_all_fwd<BLACK,FWD_EVASIONS>(pos, moveList, checkers) : generate_all_fwd<BLACK,FWD_NON_EVASIONS>(pos, moveList,  checkers);
    
    while (cur != moveList) {
        if (pinned & cur->from_sq() && !(line_bb(cur->from_sq(), cur->to_sq()) & ksq)) {
            *cur = *(--moveList);
        } else if (cur->from_sq() == ksq && pos.attackers_to_exist(cur->to_sq(), pos.pieces() ^ ksq, ~us)) {
            *cur = *(--moveList);
        } else {
            ++cur;
        }
    }
    return moveList;
}

template<Color Us,EGGenType Type>
Move* generate_rev_pawn_moves(const EGPosition& pos, Move* moveList) {

    // constexpr Color     Them     = ~Us;
    constexpr Bitboard  TRank2BB = (Us == WHITE ? Rank2BB : Rank7BB);
    constexpr Bitboard  TRank3BB = (Us == WHITE ? Rank3BB : Rank6BB);
    constexpr Direction Down     = (Us == WHITE ? SOUTH: NORTH);

    const Bitboard emptySquares = ~pos.pieces();
    Bitboard pawnsNotOn2 = pos.pieces(Us, PAWN) & ~TRank2BB;


    // Single and double pawn pushes, no promotions
    {
        Bitboard b1 = shift<Down>(pawnsNotOn2) & emptySquares;
        Bitboard b2 = shift<Down>(b1 & TRank3BB) & emptySquares;

        moveList = splat_pawn_moves<Down>(moveList, b1);
        moveList = splat_pawn_moves<Down + Down>(moveList, b2);
    }

    // no captures

    return moveList;
}

template<Color Us, EGGenType Type>
Move* generate_all_rev(const EGPosition& pos, Move* moveList) {
    Square   ksq    = pos.square<KING>(Us);

    Bitboard target = ~pos.pieces(); // non-captures

    moveList = generate_rev_pawn_moves<Us, Type>(pos, moveList);
    moveList = generate_moves<Us, KNIGHT>(pos, moveList, target);
    moveList = generate_moves<Us, BISHOP>(pos, moveList, target);
    moveList = generate_moves<Us, ROOK>(pos, moveList, target);
    moveList = generate_moves<Us, QUEEN>(pos, moveList, target);
    
    Bitboard b = attacks_bb<KING>(ksq) & target;
    moveList = splat_moves(moveList, ksq, b);

    return moveList;
}

bool attackers_to_exist_after_moving_piece(Square s, Color c, Bitboard byTypeBB[PIECE_TYPE_NB], Bitboard byColorBB[COLOR_NB]) {
    Bitboard occupied = byTypeBB[ALL_PIECES];
    return ((attacks_bb<ROOK>(s) & (byTypeBB[ROOK] | byTypeBB[QUEEN]) & byColorBB[c])
            && (attacks_bb<ROOK>(s, occupied) & (byTypeBB[ROOK] | byTypeBB[QUEEN]) & byColorBB[c]))
        || ((attacks_bb<BISHOP>(s) & (byTypeBB[BISHOP] | byTypeBB[QUEEN]) & byColorBB[c])
            && (attacks_bb<BISHOP>(s, occupied) & (byTypeBB[BISHOP] | byTypeBB[QUEEN]) & byColorBB[c]))
        || (((attacks_bb<PAWN>(s, ~c) & byTypeBB[PAWN]) | (attacks_bb<KNIGHT>(s) & byTypeBB[KNIGHT])
             | (attacks_bb<KING>(s) & byTypeBB[KING]))
            & byColorBB[c]);
}
Bitboard attackers_to_after_moving_piece(Square s, Bitboard byTypeBB[PIECE_TYPE_NB], Bitboard byColorBB[COLOR_NB]) {
    Bitboard occupied = byTypeBB[ALL_PIECES];
    return (attacks_bb<ROOK>(s, occupied) & (byTypeBB[ROOK] | byTypeBB[QUEEN]))
         | (attacks_bb<BISHOP>(s, occupied) & (byTypeBB[BISHOP] | byTypeBB[QUEEN]))
         | (attacks_bb<PAWN>(s, BLACK) & byTypeBB[PAWN] & byColorBB[WHITE])
         | (attacks_bb<PAWN>(s, WHITE) & byTypeBB[PAWN] & byColorBB[BLACK])
         | (attacks_bb<KNIGHT>(s) & byTypeBB[KNIGHT]) | (attacks_bb<KING>(s) & byTypeBB[KING]);
}

template<>
Move* generate<REVERSE>(const EGPosition& pos, Move* moveList) {

    Color    us     = ~pos.side_to_move();
    Move*    cur    = moveList;
    Square our_ksq = pos.square<KING>(us);
    Square their_ksq = pos.square<KING>(~us);

    moveList = (us == WHITE) ? generate_all_rev<WHITE, REVERSE>(pos, moveList) : generate_all_rev<BLACK, REVERSE>(pos, moveList);

    Bitboard byTypeBB[PIECE_TYPE_NB];
    Bitboard byColorBB[COLOR_NB];
    for (PieceType pt = ALL_PIECES; pt <= KING; ++pt) byTypeBB[pt] = pos.pieces(pt);
    byColorBB[us] = pos.pieces(us);
    byColorBB[~us] = pos.pieces(~us);

    while (cur != moveList) {
        Square from = cur->from_sq();
        Square to = cur->to_sq();
        PieceType pt = type_of(pos.piece_on(from));

        Bitboard fromTo = from | to;
        byTypeBB[ALL_PIECES] ^= fromTo;
        byTypeBB[pt] ^= fromTo;
        byColorBB[us] ^= fromTo;

        if (attackers_to_exist_after_moving_piece(their_ksq, us, byTypeBB, byColorBB)) {
            *cur = *(--moveList);
        } else if (cur->from_sq() != our_ksq && popcount(attackers_to_after_moving_piece(our_ksq, byTypeBB, byColorBB) & byColorBB[~us]) > 1) {
            // if our king is in double check after reversing move, only legal moves are king moves
            *cur = *(--moveList);
        } else {
            ++cur;
        }

        // Bitboard toFrom = to | from;
        byTypeBB[ALL_PIECES] ^= fromTo;;
        byTypeBB[pt] ^= fromTo;
        byColorBB[us] ^= fromTo;
    }
    return moveList;
}

template<EGGenType T>
struct EGMoveList {
    explicit EGMoveList(const EGPosition& pos) :
        last(generate<T>(pos, moveList)) {}
    const Move* begin() const { return moveList; }
    const Move* end() const { return last; }
    size_t      size() const { return last - moveList; }

private:
    Move moveList[MAX_MOVES], *last;
};

#endif