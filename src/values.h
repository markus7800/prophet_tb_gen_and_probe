#ifndef VALUES_H_INCLUDED
#define VALUES_H_INCLUDED

#include <cstdint>

namespace Prophet {

#define UNUSED 1111
#define UNKNOWN 1110
inline int16_t WIN_IN(int16_t level) { return 1000 - level; }
inline int16_t LOSS_IN(int16_t level) { return -1000 + level; }
inline int16_t MAYBELOSS_IN(int16_t level) { return -11000 + level; }
// a position is MAYBELOSS_IN(level) if there is a move to position that is WIN_IN(level-1) and all other moves are <=WIN_IN(level-1) (draw and loss is possible)
inline int16_t IS_SET(int16_t val) { return LOSS_IN(0) <= val && val <= WIN_IN(0); }

#define LOWERBOUND_OFFSET 11000
inline int16_t IS_LOWERBOUND(int16_t val) { return LOSS_IN(0) + LOWERBOUND_OFFSET <= val && val <= WIN_IN(0) + LOWERBOUND_OFFSET; };
inline int16_t VAL_TO_LOWERBOUND(int16_t val) { return val + LOWERBOUND_OFFSET; };
inline int16_t LOWERBOUND_TO_VAL(int16_t lb) { return lb - LOWERBOUND_OFFSET; };

} // namespace Prophet

#endif
