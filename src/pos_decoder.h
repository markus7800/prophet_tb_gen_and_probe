#ifndef POS_DECODER_H_INCLUDED
#define POS_DECODER_H_INCLUDED

#include <vector>
#include "linearize.h"

/*
Fast decoding of indices into positions, equivalent to pos_at_ix_ (same index, same resulting EGPosition).

IndexLayout holds per-table data that is precomputed once for every part of the table (radices per king pair and
group, and the square lists of side-to-move groups, whose domains depend only on the king pair). It is shared
between threads. It is created lazily by EGTB::get_layout, so tables that are only probed do not allocate it.

PosDecoder is per thread. Compared to pos_at_ix_ it
- caches the part and the king pair slice of the last index (no binary search for nearby indices),
- steps the mixed-radix digits instead of dividing when called with consecutive indices,
- uses 32-bit divisions when every king pair slice of a part has less than 2^32 indices,
- decodes combinations of up to 4 identical pieces with lookup tables,
- finds squares of side-to-move groups in precomputed lists and squares of side-not-to-move groups by
  stepping over the (at most 6) occupied squares instead of iterating over set bits.
En-passant indices are decoded with pos_at_ix_.
*/

struct IndexLayout {
    const TableIndex* index; // owned by the EGTB that owns the layout
    bool no_pawns;
    int n_kk;

    struct Part {
        uint64_t start;
        uint64_t nonep_size;
        std::vector<uint64_t> poscounts; // n_kk + 1 entries, start of every king pair slice within the part
        bool small_slices; // every slice has less than 2^32 indices

        // groups of identical pieces in placement order (least significant digit first)
        // pawns of split tables form one group per file class (group_class), otherwise group_class is -1
        int n_groups;
        bool group_is_stm[12];
        PieceType group_pt[12];
        int group_count[12];
        int group_class[12];

        std::vector<uint32_t> radix;      // [kix * n_groups + g]
        std::vector<uint8_t> stm_squares; // [(kix * n_groups + g) * 64 + rank] for side-to-move groups
    };
    std::vector<Part> parts;

    explicit IndexLayout(const TableIndex& t);
};

struct PosDecoder {
    const IndexLayout* L;
    Color stm;

    bool valid; // state below describes prev_ix
    uint64_t prev_ix;
    int part;
    int kix;
    uint32_t digits[12];

    PosDecoder(const IndexLayout* layout, Color stm_) : L(layout), stm(stm_), valid(false), prev_ix(0), part(0), kix(0) {}

    // pos is reset and set to the position at ix (side to move stm)
    void pos_at_ix(EGPosition &pos, uint64_t ix);

private:
    void find_slice(uint64_t local_ix);
    void split_digits(uint64_t local_ix);
    void place_pieces(EGPosition &pos) const;
};

#endif
