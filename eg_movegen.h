
#ifndef EG_MOVEGEN_H_INCLUDED
#define EG_MOVEGEN_H_INCLUDED

#include "eg_position.h"

enum EGGenType {
    FORWARD, // all legal moves except castling and en-passant (for now)
    FWD_EVASIONS, // if in check all legal evasions
    FWD_NON_EVASIONS,
    REVERSE,
};


Move* generate_forward(const EGPosition& pos, Move* moveList);

struct EGMoveList {
    explicit EGMoveList(const EGPosition& pos) :
        last(generate_forward(pos, moveList)) {}
    const Move* begin() const { return moveList; }
    const Move* end() const { return last; }
    size_t      size() const { return last - moveList; }

private:
    Move moveList[MAX_MOVES], *last;
};

Move* generate_reverse(const EGPosition& pos, Move* moveList, PieceType captured, PieceType promotion);

struct EGUndoInfoList {
    explicit EGUndoInfoList(EGPosition& pos, PieceType captured = NO_PIECE_TYPE, PieceType promotion = NO_PIECE_TYPE) {
        last_move = generate_reverse(pos, moveList, captured, promotion);
        bool ep_possible = pos.count(WHITE, PAWN) > 0 && pos.count(BLACK, PAWN) > 0;
        last = undoInfoList;
        Move* cur = moveList;
        while (cur != last_move) {
            Move m = *cur++;
            UndoInfo rev_move = UndoInfo(m, captured, SQ_NONE);
            *last++ = rev_move;
            if (ep_possible) {
                pos.do_rev_move(rev_move);
                Bitboard ep_candiates = pos.ep_candidates();
                while (ep_candiates) {
                    Square ep_sq = pop_lsb(ep_candiates);
                    if (pos.check_ep(ep_sq)) {
                        *last++ = UndoInfo(m, captured, ep_sq);
                    }
                }
                pos.undo_rev_move(rev_move);
            }
        }
    }
    const UndoInfo* begin() const { return undoInfoList; }
    const UndoInfo* end() const { return last; }
    size_t      size() const { return last - undoInfoList; }

private:
    Move moveList[MAX_MOVES], *last_move;
    UndoInfo undoInfoList[MAX_MOVES], *last;
};

#endif