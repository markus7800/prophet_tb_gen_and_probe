
#ifndef LINEARIZE_H_INCLUDED
#define LINEARIZE_H_INCLUDED

#include "kkx.h"
#include "eg_position.h"
#include "uci.h"
#include "triangular_indexes.h"

#include <vector>

// index of tables that are not split into parts (see TableIndex)
void compute_poscounts(const int stm_pieces[6], const int sntm_pieces[6], uint64_t kntm_poscounts[], uint64_t& num_nonep_pos, uint64_t& num_ep_pos, uint64_t& num_pos);

/*
Tables with 1 or 2 pawns are split into parts by the file classes of their pawns. The file class of a square is
min(file, 7 - file), i.e. a/h, b/g, c/f, d/e. Non-capture moves never change the file of a pawn, and the left-right
mirroring of the index does not change file classes, so every position reachable without captures and promotions
(which leave the table) has the same multiset of pawn classes for each side. A part is one such combination:

    (multiset of stm pawn classes, multiset of sntm pawn classes)

1 pawn: 4 parts, 2 pawns of one side: 10 parts, one pawn each: 16 parts. Parts are independent of each other during
generation and are stored as separate files. The global index is part-major: every part is a contiguous range
[start, start + nonep_size + ep_size), first its non-en-passant positions, then its en-passant positions.
Within the non-en-passant section of a part, the index is like the index of an unsplit table (king pair slices,
mixed radix digits), except that the pawns of each class form their own group, whose domain are the pawn squares
on the two files of the class. En-passant positions are only possible with one pawn each (16 parts), the en-passant
configurations of a part are those whose two pawns have the classes of the part.
Tables without pawns or with more than 2 pawns are not split: they consist of a single part with the original index.
*/

#define MAX_PARTS 16

inline int file_class(Square sq) {
    int f = sq & 7;
    return f < 4 ? f : 7 - f;
}
// the two files of a file class
inline Bitboard class_files_bb(int c) {
    return (FileABB << c) | (FileABB << (7 - c));
}

// domain of the stm pawns of class c in split tables (same exclusions as for unsplit tables, restricted to the files of c)
inline Bitboard stm_pawn_class_domain(int c, Square kntm_sq, Square ktm_sq) {
    return PawnSquaresBB & class_files_bb(c) & ~(unblockablechecks_bb(kntm_sq, PAWN) | square_bb(kntm_sq) | square_bb(ktm_sq));
}
// domain of the sntm pawns of class c in split tables, occupied contains the kings and all stm pawns
inline Bitboard sntm_pawn_class_domain(int c, Bitboard occupied) {
    return PawnSquaresBB & class_files_bb(c) & ~occupied;
}

struct PartInfo {
    int stm_class_count[4];   // number of stm pawns of each file class
    int sntm_class_count[4];  // number of sntm pawns of each file class
    uint64_t start;           // global index of the first position of the part
    uint64_t nonep_size;
    uint64_t ep_size;
    int n_ep;                 // number of en-passant configurations of the part
    int8_t ep_local[N_EP];    // en-passant configuration (EP_IX) -> local en-passant index, -1 if not in this part
    int8_t ep_global[N_EP];   // local en-passant index -> en-passant configuration
};

struct TableIndex {
    int stm_pieces[6];
    int sntm_pieces[6];
    bool no_pawns;
    bool split;
    int n_kk;
    int n_parts;
    PartInfo parts[MAX_PARTS];
    std::vector<uint64_t> poscounts; // n_parts * (n_kk + 1): start of each king pair slice within the non-ep section of a part
    uint64_t num_nonep_pos; // all parts
    uint64_t num_ep_pos;    // all parts
    uint64_t num_pos;

    const uint64_t* part_poscounts(int p) const { return &poscounts[(uint64_t) p * (n_kk + 1)]; }
    uint64_t part_end(int p) const { return parts[p].start + parts[p].nonep_size + parts[p].ep_size; }
    int part_of(uint64_t ix) const {
        int p = 0;
        while (p + 1 < n_parts && parts[p + 1].start <= ix) p++;
        return p;
    }
    bool is_ep(uint64_t ix) const {
        const PartInfo& P = parts[part_of(ix)];
        return ix - P.start >= P.nonep_size;
    }
    // part index of the given class counts, -1 if the table is not split or there is no such part
    int part_of_class_counts(const int stm_class_count[4], const int sntm_class_count[4]) const;
};

bool is_split_table(const int stm_pieces[6], const int sntm_pieces[6]);
void build_table_index(const int stm_pieces[6], const int sntm_pieces[6], TableIndex& t);

void pos_at_ix_(EGPosition &pos, uint64_t ix, Color stm, const TableIndex& t);
uint64_t ix_from_pos_(EGPosition const &pos, const TableIndex& t);

// can a position of part before_part of table before (stm promotes a pawn) lead to a position of part after_part of
// table after (the table after the promotion, the other side to move)? Always true if one of the tables is not split.
bool promotion_compatible(const TableIndex& before, int before_part, const TableIndex& after, int after_part);

void transform_to_canoncial(const EGPosition &pos, EGPosition &pos2);

void transform_to(const EGPosition &pos, EGPosition &pos2, int8_t h_flip, int8_t v_flip, int8_t swap);
// squares that transform_to with the same arguments maps into target
Bitboard transform_preimage_bb(Bitboard target, int8_t h_flip, int8_t v_flip, int8_t swap);

#endif