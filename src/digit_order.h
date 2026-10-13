#ifndef DIGIT_ORDER_H_INCLUDED
#define DIGIT_ORDER_H_INCLUDED

#include <vector>
#include <string>
#include "linearize.h"

/*
Digit order of the file index (written by finalize, see FLAG_DIGIT_ORDER in compressed_tb.h).

Within a king pair slice, the index is a mixed-radix number with one digit per group of identical pieces
(see pos_decoder.cpp). The canonical order of the groups (least significant digit first) is

    slot: 0 = stm pawns, 1 = sntm pawns, 2..5 = stm Q, R, B, N, 6..9 = sntm Q, R, B, N

A 32768 entry block of a compressed file only spans the 2-3 least significant digits, so the order of the digits
strongly influences how well a table compresses. finalize chooses an order per table. The generator and all tables
in memory always use the canonical index; only the values in a compressed file are stored in the file index:

    file index = same king pair slice, same digits, digits combined in the chosen order

The permutation only changes the order within a king pair slice, the slices and the en-passant section stay
in place. Packed format (uint64): bits 0-3 number of non-empty groups n, bits 4 + 4i .. 7 + 4i the canonical slot of
the i-th least significant digit of the file index. 0 means canonical order without a stored order.

Porting to tables that are split into parts: the order is defined on slots, the pawn groups of the classes of a
split table belong to the slots of the stm/sntm pawns and keep their relative order. Only DigitCursor (radices of a
slice) and the slice lookup have to know about parts.
*/

#define N_DIGIT_SLOTS 10

// canonical slots of the non-empty groups of a table, in canonical order
std::vector<int> nonempty_slots(const int stm_pieces[6], const int sntm_pieces[6]);
uint64_t pack_digit_order(const std::vector<int>& slots);
// false if packed is not a valid order for the table
bool unpack_digit_order(uint64_t packed, const int stm_pieces[6], const int sntm_pieces[6], std::vector<int>& slots);
// e.g. "PRr" (stm uppercase, sntm lowercase), least significant digit first
std::string digit_order_name(const std::vector<int>& slots);

// all orders of the non-empty groups (canonical order first)
std::vector<std::vector<int>> all_digit_orders(const int stm_pieces[6], const int sntm_pieces[6]);

// radix of every slot (1 for empty slots) of king pair slice kix, same formulas as compute_poscounts
void slice_radices(const int stm_pieces[6], const int sntm_pieces[6], int kix, uint64_t radix[N_DIGIT_SLOTS]);

// maps between canonical index and file index of a table; not thread-safe (one per thread, caches the last slice)
struct DigitCursor {
    int stm_pieces[6];
    int sntm_pieces[6];
    const uint64_t* kntm_poscounts; // canonical slice starts (n_kk + 1)
    int n_kk;
    uint64_t num_nonep_pos;
    int order[N_DIGIT_SLOTS]; // file digit i is canonical slot order[i]
    int n_order;
    bool identity;

    int kix;
    uint64_t radix[N_DIGIT_SLOTS];

    DigitCursor(const int stm_pieces_[6], const int sntm_pieces_[6], const uint64_t* kntm_poscounts_, uint64_t num_nonep_pos_, const std::vector<int>& slots);

    uint64_t to_file(uint64_t ix);
    uint64_t to_canonical(uint64_t file_ix);

private:
    void load_slice(uint64_t ix);
};

#endif
