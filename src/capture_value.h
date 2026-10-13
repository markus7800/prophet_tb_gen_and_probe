#ifndef CAPTURE_VALUE_H_INCLUDED
#define CAPTURE_VALUE_H_INCLUDED

#include "eg_position.h"
#include "eg_movegen.h"
#include "values.h"

/*
Capture don't-cares (FLAG_CAPTURE_DC, written by finalize): if the value of a position is obtained by a capture
(including en passant and capture-promotions), the stored value may be anything that is not better than the true
value. The true value is max(stored value, best_capture_value), see probe_position_raw_dctx.
Non-capture promotions are not used, so that finalize and the generator only need the capture tables in memory.
*/

enum CaptureValueStatus { NO_CAPTURE, CAPTURE_VALUE, CAPTURE_TABLE_MISSING };

// best value of pos over all captures, adjusted by one ply like the value of pos (value of the side to move of pos)
// child(pos) returns the raw value of the position after a capture (side to move after the capture) or a value
// > WIN_IN(0) or < -WIN_IN(0) if it is not available; pos is restored on return
template <typename ChildValue>
inline CaptureValueStatus best_capture_value(EGPosition& pos, ChildValue child, int16_t& value) {
    bool any = false;
    int16_t max_val = LOSS_IN(0);
    for (Move move : EGMoveList(pos)) {
        bool capture = pos.piece_on(move.to_sq()) != NO_PIECE || move.type_of() == EN_PASSANT;
        if (!capture) continue;
        UndoInfo u = pos.do_move(move);
        int16_t child_value = child(pos);
        pos.undo_move(u);
        if (child_value > WIN_IN(0) || child_value < -WIN_IN(0)) return CAPTURE_TABLE_MISSING;
        any = true;
        max_val = std::max(max_val, (int16_t) -child_value);
    }
    if (!any) return NO_CAPTURE;
    if (max_val > 0) max_val--;
    if (max_val < 0) max_val++;
    value = max_val;
    return CAPTURE_VALUE;
}

#endif
