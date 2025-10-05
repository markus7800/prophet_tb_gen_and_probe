
#ifndef LINEARIZE_H_INCLUDED
#define LINEARIZE_H_INCLUDED

#include "kkx.h"
#include "eg_position.h"
#include "uci.h"
#include "triangular_indexes.h"


uint64_t compute_num_nonep_positions(const int stm_pieces[6], const int sntm_pieces[6]) {
    int n_pawns = stm_pieces[PAWN] + sntm_pieces[PAWN];
    if (n_pawns == 0) {
        uint64_t n = N_KKX;
        uint64_t s = 62;
        
        for (int stm = 0; stm <= 1; ++stm) {
            const int* pieces = (stm) ? stm_pieces : sntm_pieces;
            for (PieceType i = KNIGHT; i < KING; ++i) {
                n *= number_of_ordered_tuples(s, pieces[i]);
                s -= pieces[i];
            }
        }

        return n;

    } else {
        uint64_t n = 1;
        uint64_t s = 64;

        // pawns
        uint64_t sp = 48;
        bool first_pawn = true;
        if (sntm_pieces[PAWN] > 0) {
            n *= number_of_ordered_tuples_with_first_pawn(sntm_pieces[PAWN]);
            s -= sntm_pieces[PAWN];
            sp -= sntm_pieces[PAWN];
            first_pawn = false;
        }
        if (stm_pieces[PAWN] > 0) {
            if (first_pawn) {
                n *= number_of_ordered_tuples_with_first_pawn(stm_pieces[PAWN]);
            } else {
                n *= number_of_ordered_tuples(sp, stm_pieces[PAWN]);
            }
            s -= stm_pieces[PAWN];
        }


        // kings
        n *= s;
        s--;
        n *= s;
        s--;

        for (int stm = 0; stm <= 1; ++stm) {
            const int* pieces = (stm) ? stm_pieces : sntm_pieces;
            for (PieceType i = KNIGHT; i < KING; ++i) {
                n *= number_of_ordered_tuples(s, pieces[i]);
                s -= pieces[i];
            }
        }

        return n;
    }
}


uint64_t compute_num_ep_positions(const int stm_pieces[6], const int sntm_pieces[6]) {
    if ( stm_pieces[PAWN] == 0 || sntm_pieces[PAWN] == 0) {
        return 0;

    } else {
        uint64_t n = 7;
        uint64_t s = 64;

        // pawns
        uint64_t sp = 46;
        n *= number_of_ordered_tuples(sp, sntm_pieces[PAWN] - 1);
        s -= sntm_pieces[PAWN];
        n *= number_of_ordered_tuples(sp, stm_pieces[PAWN] - 1);
        s -= stm_pieces[PAWN];
        

        // kings
        n *= s;
        s--;
        n *= s;
        s--;

        for (int stm = 0; stm <= 1; ++stm) {
            const int* pieces = (stm) ? stm_pieces : sntm_pieces;
            for (PieceType i = KNIGHT; i < KING; ++i) {
                n *= number_of_ordered_tuples(s, pieces[i]);
                s -= pieces[i];
            }
        }

        return n;
    }
}

uint64_t compute_num_positions(const int stm_pieces[6], const int sntm_pieces[6]) {
    return compute_num_ep_positions(stm_pieces, sntm_pieces) + compute_num_nonep_positions(stm_pieces, sntm_pieces);
}

                            // {    0,     1,     2,     3,     4,     5,     6};
const Square EP_PAWN[7]      = {SQ_A5, SQ_B5, SQ_B5, SQ_C5, SQ_C5, SQ_D5, SQ_D5};
const Square EP_CAP_PAWN[7]  = {SQ_B5, SQ_A5, SQ_C5, SQ_B5, SQ_D5, SQ_C5, SQ_E5};

const uint64_t EP_IX[4][3] = {
    {0, 0, 0}, // SQ_A5
    {1, 0, 2}, // SQ_B5
    {3, 0, 4}, // SQ_C5
    {5, 0, 6}, // SQ_D5
};

void insert_increment_sq(Square occupied_sqs[6], int& n_occupied_sqs, Square& sq) {
    int k = 0;
    for (int j = 0; j < n_occupied_sqs; j++) {
        if (occupied_sqs[j] <= sq) {
            ++sq;
            k = j + 1;
        }
    }
    for (int j = n_occupied_sqs-1; j >= k; j--) {
        occupied_sqs[j+1] = occupied_sqs[j];
    }
    n_occupied_sqs++;
    occupied_sqs[k] = sq;
}

int insert_count_lt_squares(Square occupied_sqs[6], int& n_occupied_sqs, Square sq) {
    int k = 0;
    for (int j = 0; j < n_occupied_sqs; j++) {
        k += (occupied_sqs[j] < sq);
    }
    occupied_sqs[n_occupied_sqs] = sq;
    n_occupied_sqs++;
    return k;
}

void pos_at_ix_kkx(EGPosition &pos, uint64_t ix, Color stm, int wpieces[6], int bpieces[6]) {
    assert (wpieces[PAWN] + bpieces[PAWN] == 0);
    pos.set_side_to_move(stm);

    bool is_diag_symmetric = true;
    Square occupied_sqs[6];

    uint64_t kkx = ix % N_KKX;
    ix = ix / N_KKX;

    Square ktm_sq = Square(KKX_KTM_SQ[kkx]);
    is_diag_symmetric = is_diag_symmetric && (ktm_sq & DiagBB);
    Square kntm_sq = Square(KKX_KNTM_SQ[kkx]);
    is_diag_symmetric = is_diag_symmetric && (kntm_sq & DiagBB);

    if (ktm_sq < kntm_sq) {
        occupied_sqs[0] = ktm_sq;
        occupied_sqs[1] = kntm_sq;
    } else {
        occupied_sqs[0] = kntm_sq;
        occupied_sqs[1] = ktm_sq;
    }
    pos.put_piece(make_piece(stm, KING), ktm_sq);
    pos.put_piece(make_piece(~stm, KING), kntm_sq);

    Piece pieces[4] = {NO_PIECE,NO_PIECE,NO_PIECE,NO_PIECE};
    int piece_counts[4] = {0,0,0,0};
    int total_piece_count = 2;
    int i = 0;
    for (Color c: {~stm, stm}) {
        int* c_pieces = (c == WHITE) ? wpieces : bpieces;
        for (PieceType p : {QUEEN, ROOK, BISHOP, KNIGHT}) {
            if (c_pieces[p] == 0) { continue; }
            pieces[i] = make_piece(c, p);
            piece_counts[i] = c_pieces[p];
            i++;
            total_piece_count += piece_counts[i];
            if (total_piece_count >= 6) { std::cout << "More than 6 pieces not supported!\n"; assert(false); }
        }
    }


    int n_occupied_sqs = 2;

    int sqs_ixs[4];

    // this also generates position where first off-diagonal piece is not on bottom half
    // but these indexes are unused anyways
    for (int l = 0; l < 4; l++) {
        Piece p = pieces[l];
        int piece_count = piece_counts[l];
        if (p == NO_PIECE) { break; }
        uint64_t s = number_of_ordered_tuples(64 - n_occupied_sqs, piece_count);

        uint64_t tril_ix = ix % s;
        ix = ix / s;

        tril_from_linear(piece_count, tril_ix, sqs_ixs);
        for (int j = 0; j < piece_count; j++) {
            Square sq = Square(sqs_ixs[j]-j);

            insert_increment_sq(occupied_sqs, n_occupied_sqs, sq);

            pos.put_piece(p, sq);
        }
    }
}

void pos_at_ix_kkp(EGPosition &pos, uint64_t ix, Color stm, int wpieces[6], int bpieces[6], uint64_t num_nonep_pos) {
    assert (wpieces[PAWN] + bpieces[PAWN] > 0);
    pos.set_side_to_move(stm);

    int8_t flip = stm == BLACK ? 56 : 0;

    Square occupied_sqs[6];

    Piece pieces[6] = {NO_PIECE,NO_PIECE,NO_PIECE,NO_PIECE,NO_PIECE,NO_PIECE};
    int piece_counts[6] = {0,0,0,0,0,0};
    int total_piece_count = 0;
    int i = 0;

    Color STM[12] =        {~stm,  stm, ~stm,  ~stm, ~stm,   ~stm,   ~stm,  stm,   stm,  stm,    stm,    stm};
    PieceType PIECES[12] = {PAWN, PAWN, KING, QUEEN, ROOK, BISHOP, KNIGHT, KING, QUEEN, ROOK, BISHOP, KNIGHT};

    bool EP = false;
    /*
    bool EP = ix > num_nonep_pos;
    uint64_t ep_ix = ix % 7;
    ix = ix / 7;
    pos.put_piece(make_piece(~stm, PAWN), EP_PAWN[ep_ix]);
    pos.put_piece(make_piece(stm, PAWN), EP_CAP_PAWN[ep_ix]);
    pos.set_ep_square(EP_PAWN[ep_ix] + NORTH);
    */

    for (int j = 0; j < 12; j++) {
        Color c = STM[j];
        PieceType p = PIECES[j];
        int* c_pieces = (c == WHITE) ? wpieces : bpieces;
        if (p == KING)
            piece_counts[i] = 1;
        else
            piece_counts[i] = c_pieces[p] - (EP && p == PAWN);
        if (piece_counts[i] == 0) { continue; }
        pieces[i] = make_piece(c, p);
        i++;
        total_piece_count += piece_counts[i];
        if (total_piece_count >= 6) { std::cout << "More than 6 pieces not supported!\n"; assert(false); }
    }

    int n_occupied_sqs = 0;

    int sqs_ixs[4];

    for (int l = 0; l < 6; l++) {
        Piece p = pieces[l];
        int piece_count = piece_counts[l];
        if (p == NO_PIECE) { break; }

        int offset = 0;
        if (n_occupied_sqs == 0) {
            assert(type_of(p) == PAWN);
            // first pawn
            uint64_t s = number_of_ordered_tuples_with_first_pawn(piece_count);

            uint64_t tril_ix = ix % s;
            ix = ix / s;

            int first_pawn_ix;
            pawn_tril_from_linear(piece_count, tril_ix, first_pawn_ix, sqs_ixs);
            // std::cout << "pos_at_ix first_pawn_ix: " << first_pawn_ix << std::endl;

            Square sq = Square(first_pawn_ix + (first_pawn_ix >> 2) * 4 + 8); // put on left side of board
            insert_increment_sq(occupied_sqs, n_occupied_sqs, sq);
            pos.put_piece(p, sq ^ flip);
            piece_count--;
            offset = 8 - 1;

        } else if (type_of(p) == PAWN) {
            uint64_t s = number_of_ordered_tuples(48 - n_occupied_sqs, piece_count);

            uint64_t tril_ix = ix % s;
            ix = ix / s;
            tril_from_linear(piece_count, tril_ix, sqs_ixs);
            offset = 8;

        } else {
            uint64_t s = number_of_ordered_tuples(64 - n_occupied_sqs, piece_count);
            
            uint64_t tril_ix = ix % s;
            ix = ix / s;
            tril_from_linear(piece_count, tril_ix, sqs_ixs);
        }

        for (int j = 0; j < piece_count; j++) {
            Square sq = Square(sqs_ixs[j]-j+offset);

            insert_increment_sq(occupied_sqs, n_occupied_sqs, sq);

            pos.put_piece(p, sq ^ flip);
        }
    }
}

void pos_at_ix(EGPosition &pos, uint64_t ix, Color stm, int wpieces[6], int bpieces[6], uint64_t num_nonep_pos, uint64_t num_ep_pos) {
    assert (ix < num_nonep_pos + num_ep_pos);
    if (wpieces[PAWN] + bpieces[PAWN] > 0) {
        pos_at_ix_kkp(pos, ix, stm, wpieces, bpieces, num_nonep_pos);
    } else {
        pos_at_ix_kkx(pos, ix, stm, wpieces, bpieces);
    }
}

inline Square transform(const Square sq, int8_t flip, int8_t swap) {
    int8_t sq_ix = int8_t(sq) ^ flip;
    return Square(((sq_ix >> swap) | (sq_ix << swap)) & 63);
}

inline Square maybe_update_swap_and_transform(Square sq, int8_t flip, bool& is_diag_symmetric, int8_t& swap) {
    // if this changes swap, we do not need to swap previous pieces since they are all on diagonal anyways
    if (is_diag_symmetric) {
        if (!((sq ^ flip) & DiagBB)) {
            is_diag_symmetric = false;
            swap = ((sq ^ flip) & AboveDiagBB) ? 3 : 0;
        }
    }
    return transform(sq, flip, swap);
}

inline Bitboard maybe_update_swap_and_transform_bb(Bitboard piecesBB, int8_t flip, bool& is_diag_symmetric, int8_t& swap) {
    Bitboard b = 0;
    Bitboard swapped_b = 0;
    while (piecesBB) {
        Square sq = pop_lsb(piecesBB);
        b |= square_bb(transform(sq, flip, 0));
        swapped_b |= square_bb(transform(sq, flip, 3));
    }

    if (is_diag_symmetric) {

        if (swapped_b != b) {
            is_diag_symmetric = false;
            Bitboard lower = (b & BelowDiagBB) & ~(swapped_b & BelowDiagBB);
            Bitboard lower_swapped = (swapped_b & BelowDiagBB) & ~(b & BelowDiagBB);
            if (lower_swapped == 0) {
                swap = 0;
            } else if (lower == 0) {
                swap = 3;
            } else {
                swap = lsb(lower_swapped) < lsb(lower) ? 3 : 0;
            }
        }
    }
    
    if (!swap) {
        return b;
    } else {
        return swapped_b;
    }
}


uint64_t ix_from_pos_kkx(EGPosition const &pos) {
    assert (pos.count<PAWN>(WHITE) + pos.count<PAWN>(BLACK) == 0);

    Color stm = pos.side_to_move();

    bool is_diag_symmetric = true;
    Square occupied_sqs[6];

    Square orig_ktm_sq = pos.square<KING>(stm);

    // transformation to put king in left lower quadrant
    int8_t flip = ((orig_ktm_sq & RightHalfBB) ? 7 : 0) ^ ((orig_ktm_sq & TopHalfBB) ? 56 : 0);
    // transformation to put first piece that breaks diagonal symmetry below diagonal
    int8_t swap = 0;

    Square ktm_sq = maybe_update_swap_and_transform(orig_ktm_sq, flip, is_diag_symmetric, swap);

    Square orig_kntm_sq = pos.square<KING>(~stm);
    Square kntm_sq = maybe_update_swap_and_transform(orig_kntm_sq, flip, is_diag_symmetric, swap);
    
    int16_t kkx_ix = get_kkx_ix(orig_ktm_sq, orig_kntm_sq);

    uint64_t ix = kkx_ix;
    uint64_t multiplier = N_KKX;

    if (ktm_sq < kntm_sq) {
        occupied_sqs[0] = ktm_sq;
        occupied_sqs[1] = kntm_sq;
    } else {

        occupied_sqs[0] = kntm_sq;
        occupied_sqs[1] = ktm_sq;
    }


    int sqs_ixs[4];
    uint64_t n_available_squares = 62;
    int n_occupied_sqs = 2;

    for (Color c: {~stm, stm}) {
        for (PieceType p: {QUEEN, ROOK, BISHOP, KNIGHT}) {
            Bitboard pieceBB = pos.pieces(c, p);
            if (pieceBB) {
                Bitboard transformedBB = maybe_update_swap_and_transform_bb(pieceBB, flip, is_diag_symmetric, swap);
                int piece_count = 0;
                while (transformedBB) {
                    Square sq = pop_lsb(transformedBB);
                    int k = insert_count_lt_squares(occupied_sqs, n_occupied_sqs, sq);
                    sqs_ixs[piece_count] = (int) sq - k + piece_count;
                    piece_count++;
                }
                uint64_t tril_ix = tril_to_linear(piece_count, sqs_ixs);
                ix += tril_ix * multiplier;
                multiplier *= number_of_ordered_tuples(n_available_squares, piece_count);
                n_available_squares -= piece_count;                
            }
        }
    }
    return ix;
}

inline Bitboard maybe_update_flip_and_transform_bb(Bitboard piecesBB, bool& is_horizontal_symmetric, int8_t& flip) {
    Bitboard b = 0;
    Bitboard flipped_b = 0;
    while (piecesBB) {
        Square sq = pop_lsb(piecesBB);
        b |= square_bb(transform(sq, flip, 0));
        flipped_b |= square_bb(transform(sq, flip ^ 7, 0));
    }

    int8_t horizontal_flip = 0;
    if (is_horizontal_symmetric) {

        if (flipped_b != b) {
            is_horizontal_symmetric = false;
            Bitboard left = (b & LeftHalfBB) & ~(flipped_b & LeftHalfBB);
            Bitboard left_flipped = (flipped_b & LeftHalfBB) & ~(b & LeftHalfBB);
            if (left_flipped == 0) {
                horizontal_flip = 0;
            } else if (left == 0) {
                horizontal_flip = 7;
            } else {
                horizontal_flip = lsb(left_flipped) < lsb(left) ? 7 : 0;
            }
        }
    }
    
    if (!horizontal_flip) {
        return b;
    } else {
        flip ^= horizontal_flip;
        return flipped_b;
    }
}

uint64_t ix_from_pos_kkp(EGPosition const &pos, uint64_t num_nonep_pos) {
    assert (pos.count<PAWN>(WHITE) + pos.count<PAWN>(BLACK) > 0);
    Color stm = pos.side_to_move();

    Square occupied_sqs[6];

    int8_t flip = stm == BLACK ? 56 : 0;
    bool is_horizontal_symmetric = true;

    uint64_t ix = 0;
    uint64_t multiplier = 1;
    int first_pawn_ix = 0;
    int sqs_ixs[4];
    bool first_pawn = true;
    uint64_t n_available_pawn_squares = 48;
    uint64_t n_available_squares = 64;
    int n_occupied_sqs = 0;

    bool EP = pos.ep_square() != SQ_NONE;
    Square ep_cap_pawn;
    if (EP) {
        /*
        ix += num_nonep_pos;


        Square ep_sq = pos.ep_square() ^ flip;
        if (ep_sq >= SQ_E5) {
            flip ^= 7;
            ep_sq = ep_sq ^ 7;
            is_horizontal_symmetric = false;
        }
        Bitboard pieceBB = pos.pieces(stm, PAWN);
        assert (pieceBB);
        Bitboard transformed_bb = maybe_update_flip_and_transform_bb(pieceBB, is_horizontal_symmetric, flip) & attacks_bb<PAWN>(ep_sq, ~stm);
        ep_cap_pawn = pop_lsb(transformed_bb);

        uint64_t ep_ix = EP_IX[ep_sq - SQ_A5][ep_cap_pawn - ep_sq + 1];
        ix += ep_ix;
        multiplier *= 7;

        first_pawn = false;
        n_available_pawn_squares -= 2,
        n_available_squares -= 2;
        occupied_sqs[0] = pos.ep_square() + NORTH;
        occupied_sqs[1] = ep_cap_pawn;
        n_occupied_sqs += 2;
        */
    }

    for (Color c: {~stm, stm}) {
        Bitboard pieceBB = pos.pieces(c, PAWN);
        if (pieceBB) {
            Bitboard transformed_bb = maybe_update_flip_and_transform_bb(pieceBB, is_horizontal_symmetric, flip);
            if (EP) transformed_bb = transformed_bb & ~(square_bb(pos.ep_square() + NORTH) | square_bb(ep_cap_pawn));
            int n_sq_ixs = 0;
            int piece_count = 0;
            while (transformed_bb) {
                Square sq = pop_lsb(transformed_bb);
                uint64_t k = insert_count_lt_squares(occupied_sqs, n_occupied_sqs, sq);

                if (first_pawn) {
                    first_pawn = false;
                    int sq_ix = (int) sq - k - 8;
                    sq_ix = sq_ix - (sq_ix >> 3) * 4; // map to 0,1,...,23
                    first_pawn_ix = sq_ix;
                    // std::cout << "ix_from_pos first_pawn_ix: " << first_pawn_ix << " " << square_to_uci(sq) << std::endl;
                } else {
                    sqs_ixs[n_sq_ixs] = (int) sq - k - 8 + piece_count;
                    // std::cout << "ix_from_pos pawn_ix: " << sqs_ixs[n_sq_ixs] << " " << square_to_uci(sq) << std::endl;
                    n_sq_ixs++;
                }
                piece_count++;
            }

            if (n_sq_ixs < piece_count) {
                // we must have first_pawn
                uint64_t tril_ix = pawn_tril_to_linear(piece_count, first_pawn_ix, sqs_ixs);
                // std::cout << "ix_from_pos tril_ix: " << tril_ix << " piece_count=" << piece_count << std::endl;
                ix += tril_ix * multiplier;
                multiplier *= number_of_ordered_tuples_with_first_pawn(piece_count);

            } else {
                // this has to be the second pawn set (from stm), where first pawn belongs to ~stm
                uint64_t tril_ix = tril_to_linear(piece_count, sqs_ixs);
                ix += tril_ix * multiplier;
                multiplier *= number_of_ordered_tuples(n_available_pawn_squares, piece_count);
            }
            n_available_squares -= piece_count;
            n_available_pawn_squares -= piece_count;
        }
    }


    for (Color c: {~stm, stm}) {
        for (PieceType p: {KING, QUEEN, ROOK, BISHOP, KNIGHT}) {
            Bitboard pieceBB = pos.pieces(c, p);
            if (pieceBB) {
                Bitboard transformedBB = maybe_update_flip_and_transform_bb(pieceBB, is_horizontal_symmetric, flip);
                int piece_count = 0;
                while (transformedBB) {
                    Square sq = pop_lsb(transformedBB);
                    int k = insert_count_lt_squares(occupied_sqs, n_occupied_sqs, sq);
                    sqs_ixs[piece_count] = (int) sq - k + piece_count;
                    piece_count++;
                }
                uint64_t tril_ix = tril_to_linear(piece_count, sqs_ixs);
                ix += tril_ix * multiplier;
                multiplier *= number_of_ordered_tuples(n_available_squares, piece_count);
                n_available_squares -= piece_count;                
            }
        }
    }
    return ix;
}

uint64_t ix_from_pos(EGPosition const &pos, uint64_t num_nonep_pos, uint64_t num_ep_pos) {
    if (pos.count<PAWN>(WHITE) + pos.count<PAWN>(BLACK) > 0) {
        uint64_t ix = ix_from_pos_kkp(pos, num_nonep_pos);
        assert (ix < num_nonep_pos + num_ep_pos);
        return ix;
    } else {
        uint64_t ix = ix_from_pos_kkx(pos);
        assert (ix < num_nonep_pos + num_ep_pos);
        return ix;
    }
}

void transform_to_canoncial(const EGPosition &pos, EGPosition &pos2) {
    Color stm = pos.side_to_move();

    bool is_diag_symmetric = true;
    bool is_horizontal_symmetric = true;
    int8_t flip = 0;
    int8_t swap = 0;

    Square orig_ktm_sq = pos.square<KING>(stm);

    bool has_pawns = pos.count<PAWN>(WHITE) + pos.count<PAWN>(BLACK) > 0;
    if (!has_pawns) {
        flip = ((orig_ktm_sq & RightHalfBB) ? 7 : 0) ^ ((orig_ktm_sq & TopHalfBB) ? 56 : 0);
    } else {
        flip = stm == BLACK ? 56 : 0;
        for (Color c: {~stm, stm}) {
            Bitboard transformedBB = maybe_update_flip_and_transform_bb(pos.pieces(c, PAWN), is_horizontal_symmetric, flip);
            while (transformedBB) {
                pos2.put_piece(make_piece(c,PAWN), pop_lsb(transformedBB));
            }
        }
    }

    if (!has_pawns) {
        Square ktm_sq = maybe_update_swap_and_transform(orig_ktm_sq, flip, is_diag_symmetric, swap);
        pos2.put_piece(make_piece(stm, KING), ktm_sq);

        Square orig_kntm_sq = pos.square<KING>(~stm);
        Square ktnm_sq = maybe_update_swap_and_transform(orig_kntm_sq, flip, is_diag_symmetric, swap);
        pos2.put_piece(make_piece(~stm, KING), ktnm_sq);
    }

    for (Color c: {~stm, stm}) {
        for (PieceType pt: {KING, QUEEN, ROOK, BISHOP, KNIGHT, PAWN}) {
            if (!has_pawns && pt == KING) continue; // already placed
            if ( has_pawns && pt == PAWN) continue; // already placed
            Bitboard bb = pos.pieces(c, pt);
            if (bb) {
                Bitboard transformedBB;
                if (!has_pawns)
                    transformedBB = maybe_update_swap_and_transform_bb(bb, flip, is_diag_symmetric, swap);
                else
                    transformedBB = maybe_update_flip_and_transform_bb(bb, is_horizontal_symmetric, flip);
                while (transformedBB) {
                    pos2.put_piece(make_piece(c,pt), pop_lsb(transformedBB));
                }
            }
        }
    }
    pos2.set_side_to_move(pos.side_to_move());
}

void transform_to(const EGPosition &pos, EGPosition &pos2, int8_t h_flip, int8_t v_flip, int8_t swap) {

    for (Square sq = SQ_A1; sq <= SQ_H8; ++sq) {
        Piece p = pos.piece_on(sq);
        if (p)
            pos2.put_piece(p, transform(sq, h_flip ^ v_flip, swap));
    }

    pos2.set_side_to_move(pos.side_to_move());

}

#endif