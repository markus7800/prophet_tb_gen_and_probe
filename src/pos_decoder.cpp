#include "pos_decoder.h"
#include <algorithm>

/*
Structure of the (non en-passant) index, as defined by build_table_index / pos_at_ix_ / ix_from_pos_:

    ix = part.start + poscounts_part[kix] + local_ix

Tables with 1 or 2 pawns are split into parts by the file classes of their pawns (see linearize.h); every
part is a contiguous index range with its own king pair slices. Tables that are not split have one part.

kix enumerates the canonical king pairs (N_KKX without pawns, N_KKP with pawns) and poscounts[kix] is the
first index of the slice of king pair kix. Within a slice, the non-king pieces are split into groups of
identical pieces in a fixed order:

    stm pawns, sntm pawns, stm Q, R, B, N, sntm Q, R, B, N   (empty groups are skipped)

Every group g is encoded by one digit d_g: the rank (tril_to_linear, a combination index) of the sorted
positions of its count_g pieces within the domain of the group. The domain is an ordered set of n_g squares,
so 0 <= d_g < r_g = C(n_g, count_g). The digits form a mixed-radix number, the first group is the least
significant digit:

    local_ix = d_0 + r_0 * (d_1 + r_1 * (d_2 + r_2 * (...)))

The radices r_g depend only on the king pair (see below), so every slice is a full rectangle of
r_0 * r_1 * ... indices and decoding is: split local_ix into digits, then map every digit to squares.

Domains (squares not flipped, i.e. before the black-to-move flip of pawn tables):
- stm groups: all squares except the two kings and the squares from which a piece of the group type would
  give an unblockable check to the sntm king (sntm in check is illegal); pawns additionally exclude ranks 1
  and 8. This depends only on the king pair, so the domain is a fixed list of squares per (kix, group).
- sntm pawns: the 48 pawn squares (8..55) except kings and stm pawns. The number of these squares is fixed
  per king pair (stm pawns never collide with kings or each other), but which squares they are depends on
  the stm pawns, so the d-th domain square is the d-th free square from square 8 on.
- sntm pieces: all squares except the squares of all previously placed pieces, i.e. 62 minus the number
  of non-king pieces placed before. Again the size is fixed, the squares depend on the earlier groups.
  stm groups do not exclude each other's squares, so pieces can collide; such indices decode to broken
  positions (fewer pieces on the board), which the generator marks as unused. The radix still counts each
  piece once, exactly like compute_poscounts.

In split tables, the pawns of every file class form their own group (stm classes ascending, then sntm
classes ascending) with the pawn squares on the two files of the class as domain:
- stm pawns of class c: like stm pawns above, restricted to the files of c (fixed list per (kix, group)).
- sntm pawns of class c: the pawn squares on the files of c except kings and stm pawns. The number of stm
  pawns on these files is known from the part, so the radix is fixed per king pair.

Within a group, all pieces are placed relative to the occupancy before the group (as in pos_at_ix_).
*/

// ---- layout

IndexLayout::IndexLayout(const TableIndex& t) {
    index = &t;
    no_pawns = t.no_pawns;
    n_kk = t.n_kk;
    const int* stm_pieces = t.stm_pieces;
    const int* sntm_pieces = t.sntm_pieces;
    int stm_pawns = stm_pieces[PAWN];
    int sntm_pawns = sntm_pieces[PAWN];

    parts.resize(t.n_parts);
    for (int p = 0; p < t.n_parts; p++) {
        const PartInfo& P = t.parts[p];
        Part& L = parts[p];
        L.start = P.start;
        L.nonep_size = P.nonep_size;
        L.poscounts.assign(t.part_poscounts(p), t.part_poscounts(p) + n_kk + 1);
        assert (L.poscounts[n_kk] == L.nonep_size);
        L.small_slices = true;
        for (int kix = 0; kix < n_kk; kix++) {
            L.small_slices = L.small_slices && (L.poscounts[kix + 1] - L.poscounts[kix] < (1ULL << 32));
        }

        // same group order as pos_at_ix_ / ix_from_pos_
        L.n_groups = 0;
        auto add_group = [&](bool is_stm, PieceType pt, int count, int cl) {
            if (count == 0) return;
            L.group_is_stm[L.n_groups] = is_stm;
            L.group_pt[L.n_groups] = pt;
            L.group_count[L.n_groups] = count;
            L.group_class[L.n_groups] = cl;
            L.n_groups++;
        };
        if (t.split) {
            for (int c = 0; c < 4; c++) add_group(true, PAWN, P.stm_class_count[c], c);
            for (int c = 0; c < 4; c++) add_group(false, PAWN, P.sntm_class_count[c], c);
        } else {
            add_group(true, PAWN, stm_pawns, -1);
            add_group(false, PAWN, sntm_pawns, -1);
        }
        for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) add_group(true, pt, stm_pieces[pt], -1);
        for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) add_group(false, pt, sntm_pieces[pt], -1);

        // per king pair: radix r_g = C(n_g, count_g) of every group and, for stm groups, the domain as a sorted
        // square list, so that the d-th domain square is a table lookup (pos_at_ix_ uses nth_set_sq on a mask)
        int n_groups = L.n_groups;
        L.radix.assign((uint64_t) n_kk * n_groups, 0);
        L.stm_squares.assign((uint64_t) n_kk * n_groups * 64, 0);
        for (int kix = 0; kix < n_kk; kix++) {
            Square kntm_sq = no_pawns ? Square(KKX_KNTM_SQ[kix]) : Square(KKP_KNTM_SQ[kix]);
            Square ktm_sq  = no_pawns ? Square(KKX_KTM_SQ[kix]) : Square(KKP_KTM_SQ[kix]);
            Bitboard kings = square_bb(kntm_sq) | square_bb(ktm_sq);
            // sntm pieces are placed after all pawns and stm pieces, each sntm group removes its squares for the next
            uint64_t n_squares_available_to_sntm = 62 - stm_pawns - sntm_pawns;
            for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) n_squares_available_to_sntm -= stm_pieces[pt];

            for (int g = 0; g < n_groups; g++) {
                PieceType pt = L.group_pt[g];
                int cl = L.group_class[g];
                uint64_t n_domain;
                if (L.group_is_stm[g]) {
                    Bitboard available;
                    if (pt == PAWN && cl >= 0) {
                        available = stm_pawn_class_domain(cl, kntm_sq, ktm_sq);
                    } else {
                        available = ~(unblockablechecks_bb(kntm_sq, pt) | kings);
                        if (pt == PAWN) available &= ~(Rank1BB | Rank8BB);
                    }
                    n_domain = popcount(available);
                    Bitboard b = available;
                    uint64_t r = 0;
                    while (b) L.stm_squares[((uint64_t) kix * n_groups + g) * 64 + r++] = pop_lsb(b);
                } else if (pt == PAWN && cl >= 0) {
                    // pawn squares on the files of the class minus kings minus the stm pawns of the class
                    n_domain = popcount(PawnSquaresBB & class_files_bb(cl) & ~kings) - P.stm_class_count[cl];
                } else if (pt == PAWN) {
                    // pawn squares minus stm pawns minus kings standing on pawn squares
                    n_domain = 48 - stm_pawns - bool(square_bb(kntm_sq) & PawnSquaresBB) - bool(square_bb(ktm_sq) & PawnSquaresBB);
                } else {
                    n_domain = n_squares_available_to_sntm;
                    n_squares_available_to_sntm -= L.group_count[g];
                }
                L.radix[(uint64_t) kix * n_groups + g] = number_of_ordered_tuples(n_domain, L.group_count[g]);
            }
        }
    }
}

// ---- decoder

// k-th (0-based) square in [first, 64) that is not in occ
// start with square first + k; every occupied square at or below the candidate pushes it up by one
// (occ holds at most 6 squares, whereas nth_set_sq(~occ, k) iterates over k set bits without PDEP)
inline Square kth_free_square(Bitboard occ, int first, int k) {
    Square sq = Square(first + k);
    occ &= ~(square_bb(Square(first)) - 1); // only occupied squares >= first shift the result
    while (occ) {
        Square o = lsb(occ);
        if (o > sq) break;
        sq = Square(sq + 1);
        occ &= occ - 1;
    }
    return sq;
}

// sets kix to the king pair slice of the current part that contains local_ix: poscounts[kix] <= local_ix < poscounts[kix + 1]
// the generator visits indices in increasing order, so first try the current and the next slice
void PosDecoder::find_slice(uint64_t local_ix) {
    const std::vector<uint64_t>& pc = L->parts[part].poscounts;
    if (kix < L->n_kk && pc[kix] <= local_ix && local_ix < pc[kix + 1]) return;
    if (kix + 1 < L->n_kk && pc[kix + 1] <= local_ix && local_ix < pc[kix + 2]) { kix++; return; }
    kix = std::upper_bound(pc.begin(), pc.end(), local_ix) - pc.begin() - 1;
}

// local_ix = d_0 + r_0 * (d_1 + r_1 * (d_2 + ...)): peel off the least significant digit first
// (pos_at_ix_ does the same with 64-bit division; if every slice has less than 2^32 indices,
// local_ix fits into 32 bits and the cheaper 32-bit division gives the same digits)
void PosDecoder::split_digits(uint64_t local_ix) {
    const IndexLayout::Part& P = L->parts[part];
    const uint32_t* r = &P.radix[(uint64_t) kix * P.n_groups];
    if (P.small_slices) {
        uint32_t x = (uint32_t) local_ix;
        for (int g = 0; g < P.n_groups; g++) {
            digits[g] = x % r[g];
            x /= r[g];
        }
    } else {
        uint64_t x = local_ix;
        for (int g = 0; g < P.n_groups; g++) {
            digits[g] = x % r[g];
            x /= r[g];
        }
    }
}

void PosDecoder::place_pieces(EGPosition &pos) const {
    const IndexLayout::Part& P = L->parts[part];
    pos.reset();
    pos.set_side_to_move(stm);
    int8_t flip = (!L->no_pawns && stm == BLACK) ? 56 : 0; // black pawn to move flip

    Square kntm_sq = L->no_pawns ? Square(KKX_KNTM_SQ[kix]) : Square(KKP_KNTM_SQ[kix]);
    Square ktm_sq  = L->no_pawns ? Square(KKX_KTM_SQ[kix]) : Square(KKP_KTM_SQ[kix]);
    pos.put_piece(make_piece(stm, KING), Square(ktm_sq ^ flip));
    pos.put_piece(make_piece(~stm, KING), Square(kntm_sq ^ flip));
    Bitboard occupied = square_bb(kntm_sq) | square_bb(ktm_sq); // not flipped

    int ranks[8];
    Square sqs[8];
    // groups have to be placed in digit order, because the domains of sntm groups depend on all earlier groups
    for (int g = 0; g < P.n_groups; g++) {
        int count = P.group_count[g];
        PieceType pt = P.group_pt[g];
        // digit -> sorted positions within the domain (combination index of count identical pieces)
        fast_tril_from_linear(count, digits[g], ranks);
        // positions within the domain -> squares
        // all pieces of a group are placed relative to the occupancy before the group (as in pos_at_ix_)
        if (P.group_is_stm[g]) {
            const uint8_t* list = &P.stm_squares[((uint64_t) kix * P.n_groups + g) * 64];
            for (int j = 0; j < count; j++) sqs[j] = Square(list[ranks[j]]);
        } else if (pt == PAWN && P.group_class[g] >= 0) {
            // sntm pawns of a class: at most 12 squares, the other sntm pawn classes are on other files
            Bitboard domain = sntm_pawn_class_domain(P.group_class[g], occupied);
            for (int j = 0; j < count; j++) sqs[j] = nth_set_sq(domain, ranks[j]);
        } else {
            int first = pt == PAWN ? 8 : 0; // pawns are on squares 8..55
            for (int j = 0; j < count; j++) sqs[j] = kth_free_square(occupied, first, ranks[j]);
        }
        Piece pc = make_piece(P.group_is_stm[g] ? stm : ~stm, pt);
        for (int j = 0; j < count; j++) {
            pos.put_piece(pc, Square(sqs[j] ^ flip));
            occupied |= square_bb(sqs[j]);
        }
    }
}

void PosDecoder::pos_at_ix(EGPosition &pos, uint64_t ix) {
    // part of ix (usually the same as for the previous index)
    if (!(L->parts[part].start <= ix && ix < L->index->part_end(part))) {
        part = L->index->part_of(ix);
        valid = false;
        kix = 0;
    }
    const IndexLayout::Part& P = L->parts[part];
    uint64_t local_ix = ix - P.start;
    if (local_ix >= P.nonep_size) {
        valid = false;
        pos.reset();
        pos_at_ix_(pos, ix, stm, *L->index);
        return;
    }
    if (valid && ix == prev_ix + 1 && local_ix < P.poscounts[kix + 1] && P.n_groups > 0) {
        // ix is the successor of prev_ix in the same slice: local_ix + 1 in mixed radix is an odometer step,
        // increment d_0 and carry into the next digit whenever a digit reaches its radix
        // the carry cannot run past the last digit, because local_ix + 1 < r_0 * r_1 * ... (slice size)
        const uint32_t* r = &P.radix[(uint64_t) kix * P.n_groups];
        int g = 0;
        digits[0]++;
        while (digits[g] == r[g]) {
            digits[g] = 0;
            g++;
            assert (g < P.n_groups);
            digits[g]++;
        }
    } else {
        // random access: find the slice, then compute all digits by division
        find_slice(local_ix);
        split_digits(local_ix - P.poscounts[kix]);
    }
    prev_ix = ix;
    valid = true;
    place_pieces(pos);
}
