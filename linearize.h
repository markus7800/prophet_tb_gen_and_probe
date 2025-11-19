
#ifndef LINEARIZE_H_INCLUDED
#define LINEARIZE_H_INCLUDED

#include "kkx.h"
#include "eg_position.h"
#include "uci.h"
#include "triangular_indexes.h"

void compute_kntm_poscounts(const int stm_pieces[6], const int sntm_pieces[6], uint64_t kntm_poscounts[N_KKP+1]) {
    Bitboard unblockable_checks = 0;
    Bitboard forbidden_squares = 0;
    uint64_t n_domain = 0;

    int n_pawns = stm_pieces[PAWN] + sntm_pieces[PAWN];
    int max_ix = (n_pawns == 0) ? N_KKX : N_KKP;
    kntm_poscounts[0] = 0;
    for (int ix = 0; ix < max_ix; ix++) {
        Square kntm_sq = (n_pawns == 0) ? Square(KKX_KNTM_SQ[ix]) : Square(KKP_KNTM_SQ[ix]);
        Square ktm_sq  = (n_pawns == 0) ?  Square(KKX_KTM_SQ[ix]) : Square(KKP_KTM_SQ[ix]);

        uint64_t n = 1;
        uint64_t n_squares_available_to_sntm = 62;


        // stm pawns
        unblockable_checks = unblockablechecks_bb(kntm_sq, PAWN);
        forbidden_squares = unblockable_checks | square_bb(kntm_sq) | square_bb(ktm_sq) | Rank1BB | Rank8BB;
        n_domain = 48 - num_unblockablechecks(kntm_sq, PAWN) - bool(square_bb(kntm_sq) & PawnSquaresBB) - bool(square_bb(ktm_sq) & PawnSquaresBB); // ktm cannot be in unblockable check square
        // std::cout << Bitboards::pretty(~forbidden_squares) << "kntm: " << square_to_uci(kntm_sq) << " ktm:" << square_to_uci(ktm_sq) << " " << n_domain << " vs " << popcount(~forbidden_squares) << std::endl;
        assert (n_domain == (uint64_t) popcount(~forbidden_squares));
        n *= number_of_ordered_tuples(n_domain, stm_pieces[PAWN]);
        n_squares_available_to_sntm -= stm_pieces[PAWN];

        // sntm pawns
        n_domain = 48 - stm_pieces[PAWN] - bool(square_bb(kntm_sq) & PawnSquaresBB) - bool(square_bb(ktm_sq) & PawnSquaresBB);
        n *= number_of_ordered_tuples(n_domain, sntm_pieces[PAWN]);
        n_squares_available_to_sntm -= sntm_pieces[PAWN];


        // stm_pieces
        for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) {
            unblockable_checks = unblockablechecks_bb(kntm_sq, pt);
            forbidden_squares = unblockable_checks | square_bb(kntm_sq) | square_bb(ktm_sq);
            // ktm can be in unblockable check square for knight
            n_domain = 64 - num_unblockablechecks(kntm_sq, pt) - 2 + bool(square_bb(ktm_sq) & unblockable_checks);
            assert (n_domain == (uint64_t) popcount(~forbidden_squares));

            n *= number_of_ordered_tuples(n_domain, stm_pieces[pt]);
            n_squares_available_to_sntm -= stm_pieces[pt];
        }

        // sntm pieces
        for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) {
            n *= number_of_ordered_tuples(n_squares_available_to_sntm, sntm_pieces[pt]);
            n_squares_available_to_sntm -= sntm_pieces[pt];
        }
        // std::cout << ix << ": kntm_sq:" << square_to_uci(kntm_sq) << " ktm_sq:" << square_to_uci(ktm_sq) << " " << n << " " <<  kntm_poscounts[ix] + n << std::endl;

        kntm_poscounts[ix+1] = kntm_poscounts[ix] + n;
    }
}

uint64_t compute_num_nonep_positions(const int stm_pieces[6], const int sntm_pieces[6]) {
    int n_pawns = stm_pieces[PAWN] + sntm_pieces[PAWN];
    if (n_pawns == 0) {
        uint64_t n = 462;
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

void pos_at_ix_kkx(EGPosition &pos, uint64_t ix, Color stm, const int wpieces[6], const int bpieces[6], const uint64_t kntm_poscounts[N_KKP]) {
    bool no_pawns = (wpieces[PAWN] + bpieces[PAWN] == 0);
    pos.set_side_to_move(stm);
    int8_t flip = !no_pawns && (stm == BLACK) ? 56 : 0;

    uint64_t s = 0;
    Bitboard available_squares = 0;
    uint64_t n_available_squares = 0;

    // int kntm_ix = 0;
    // while (kntm_poscounts[kntm_ix+1] <= ix) kntm_ix++;
    int l = 0;
    int r = no_pawns ? N_KKX : N_KKP;
    while (l < r) {
        int m = l + (r-l)/2;
        if (kntm_poscounts[m] <= ix) {
            l = m + 1;
        } else {
            r = m;
        }
    }
    int kix = l-1;
    // assert (kntm_poscounts[kix] <= ix && ix < kntm_poscounts[kix+1]);

    
    ix -= kntm_poscounts[kix];

    Square kntm_sq = no_pawns ? Square(KKX_KNTM_SQ[kix]) : Square(KKP_KNTM_SQ[kix]);
    Square ktm_sq = no_pawns ? Square(KKX_KTM_SQ[kix]) : Square(KKP_KTM_SQ[kix]);
    // std::cout << "pos_at_ix: " << "kix: " << kix << " kntm_sq: " << square_to_uci(kntm_sq) << " ktm_sq: " << square_to_uci(ktm_sq) << std::endl;

    pos.put_piece(make_piece(stm, KING), ktm_sq ^ flip);
    pos.put_piece(make_piece(~stm, KING), kntm_sq ^ flip);

    int n_occupied_sqs = 2;

    int sqs_ixs[4];

    Color cs[10] = {stm, ~stm, stm, stm, stm, stm, ~stm, ~stm, ~stm, ~stm};
    PieceType pts[10] = {PAWN, PAWN, QUEEN, ROOK, BISHOP, KNIGHT, QUEEN, ROOK, BISHOP, KNIGHT};

    for (int i = 0; i < 10; i++) {
        Color c = cs[i];
        PieceType pt = pts[i];
        const int* c_pieces = (c == WHITE) ? wpieces : bpieces;
        if (c_pieces[pt] == 0) { continue; }
        int piece_count = c_pieces[pt];
        Piece pc = make_piece(c, pt);

        if (c == stm) {
            Bitboard unblockable_checks = unblockablechecks_bb(kntm_sq,pt);
            if (pt == PAWN) {
                available_squares = ~(unblockable_checks | square_bb(kntm_sq) | square_bb(ktm_sq) | Rank1BB | Rank8BB);
                n_available_squares = 48 - num_unblockablechecks(kntm_sq, PAWN) - bool(square_bb(kntm_sq) & PawnSquaresBB) - bool(square_bb(ktm_sq) & PawnSquaresBB);
            } else {
                available_squares = ~(unblockable_checks | square_bb(kntm_sq) | square_bb(ktm_sq));
                n_available_squares = 64 - num_unblockablechecks(kntm_sq, pt) - 2 + bool(square_bb(ktm_sq) & unblockable_checks);
            }
        } else {
            if (pt == PAWN) {
                available_squares = ~pos.pieces() & PawnSquaresBB; // stm pawns always occupy, kings may occupy PawnSquaresBB
                n_available_squares = 48 - n_occupied_sqs + 2 - bool(square_bb(kntm_sq) & PawnSquaresBB) - bool(square_bb(ktm_sq) & PawnSquaresBB);
            } else {
                available_squares = ~pos.pieces();
                n_available_squares = 64 - n_occupied_sqs;
            }
        }

        s = number_of_ordered_tuples(n_available_squares, piece_count);
        uint64_t tril_ix = ix % s;
        ix = ix / s;
        tril_from_linear(piece_count, tril_ix, sqs_ixs);

        std::cout << "pos_at_ix: " << PieceToChar[pc] << ": n_available_squares: " << n_available_squares << " tril_ix: " << tril_ix;
        for (int j = 0; j < piece_count; j++) {
            Square sq = nth_set_sq(available_squares, sqs_ixs[j]);
            std::cout << " " << sqs_ixs[j] << "->" << square_to_uci(sq ^ flip);
            pos.put_piece(pc, sq ^ flip);
            n_occupied_sqs++;
        }
        std::cout << std::endl;
    }
    
    if (n_occupied_sqs > 6) { std::cout << "More than 6 pieces not supported! (have " << n_occupied_sqs << ")\n"; assert(false); }

    // can be broken such that popcount(pos.pieces()) != n_occupied_sqs
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
    
    int n_occupied_sqs = 0;

    bool EP = ix > num_nonep_pos;
    if (EP) {
        ix -= num_nonep_pos;
        uint64_t ep_ix = ix % 7;
        ix = ix / 7;
        Square ep_pawn = EP_PAWN[ep_ix];
        Square ep_cap_pawn = EP_CAP_PAWN[ep_ix];
        pos.put_piece(make_piece(~stm, PAWN), ep_pawn ^ flip);
        pos.put_piece(make_piece(stm, PAWN), ep_cap_pawn ^ flip);
        pos.set_ep_square((EP_PAWN[ep_ix] + NORTH) ^ flip);

        if (ep_pawn < ep_cap_pawn) {
            occupied_sqs[0] = ep_pawn;
            occupied_sqs[1] = ep_cap_pawn;
        } else {
            occupied_sqs[0] = ep_cap_pawn;
            occupied_sqs[1] = ep_pawn;
        }
        n_occupied_sqs += 2;
        // std::cout << "ep_ix: " << ep_ix << ", ep_pawn_sq: " << square_to_uci(ep_pawn) << ", ep_cap_pawn: " << square_to_uci(ep_cap_pawn) << std::endl;
    }
    

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
        total_piece_count += piece_counts[i];
        i++;
        if (total_piece_count > 6) { std::cout << "More than 6 pieces not supported!\n"; assert(false); }
    }


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

            Square sq = Square(first_pawn_ix + 8);
            insert_increment_sq(occupied_sqs, n_occupied_sqs, sq);
            pos.put_piece(p, sq ^ flip);
            piece_count--;
            offset = 8 - 1;

            // std::cout << "pos_at_ix tril_ix: " << tril_ix << std::endl;
            // std::cout << "pos_at_ix first_pawn_ix: " << first_pawn_ix << std::endl;
            // std::cout << "pos_at_ix sqs_ixs: "; for (int j = 0; j < piece_count; j++) { std::cout << sqs_ixs[j] << " "; }; std::cout << std::endl;

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

void pos_at_ix_(EGPosition &pos, uint64_t ix, Color stm, int wpieces[6], int bpieces[6], uint64_t num_nonep_pos, uint64_t num_ep_pos, uint64_t kntm_poscounts[N_KKP+1]) {
    // assert (ix < num_nonep_pos + num_ep_pos);
    pos_at_ix_kkx(pos, ix, stm, wpieces, bpieces, kntm_poscounts);
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


uint64_t ix_from_pos_kkx(EGPosition const &pos, const uint64_t kntm_poscounts[N_KKP+1]) {
    bool no_pawns = (pos.count<PAWN>(WHITE) + pos.count<PAWN>(BLACK) == 0);

    Color stm = pos.side_to_move();

    bool is_diag_symmetric = no_pawns;
    Bitboard occupied_sqs = 0;

    Square orig_kntm_sq = pos.square<KING>(~stm);

    // transformation to put king in left half
    int8_t flip = ((orig_kntm_sq & RightHalfBB) ? 7 : 0);
    if (no_pawns) {
        // put king in lower half (bottom left quadrant)
        flip = flip ^ ((orig_kntm_sq & TopHalfBB) ? 56 : 0);
    } else {
        flip ^= stm == BLACK ? 56 : 0;
    }

    // transformation to put first piece that breaks diagonal symmetry below diagonal (only relevant for no_pawns == true)
    int8_t swap = 0;

    Square kntm_sq = maybe_update_swap_and_transform(orig_kntm_sq, flip, is_diag_symmetric, swap);
    occupied_sqs |= square_bb(kntm_sq);

    Square orig_ktm_sq = pos.square<KING>(stm);
    Square ktm_sq = maybe_update_swap_and_transform(orig_ktm_sq, flip, is_diag_symmetric, swap);
    occupied_sqs |= square_bb(ktm_sq);

    int n_occupied_sqs = 2;

    int16_t kix = no_pawns ? get_kkx_ix(kntm_sq, ktm_sq) : get_kkp_ix(kntm_sq, ktm_sq);
    uint64_t ix = kntm_poscounts[kix];
    uint64_t multiplier = 1;

    // std::cout << "ix_from_pos: " << "kix: " << kix << " kntm_sq: " << square_to_uci(kntm_sq) << " ktm_sq: " << square_to_uci(ktm_sq) << std::endl;

    int sqs_ixs[4];
    Bitboard unavailable_squares;
    uint64_t n_available_squares;

    Color cs[10] = {stm, ~stm, stm, stm, stm, stm, ~stm, ~stm, ~stm, ~stm};
    PieceType pts[10] = {PAWN, PAWN, QUEEN, ROOK, BISHOP, KNIGHT, QUEEN, ROOK, BISHOP, KNIGHT};
    
    for (int i = 0; i < 10; i++) {
        Color c = cs[i];
        PieceType pt = pts[i];

        Bitboard pieceBB = pos.pieces(c, pt);
        if (pieceBB) {
            Bitboard transformedBB = maybe_update_swap_and_transform_bb(pieceBB, flip, is_diag_symmetric, swap);

            if (c == stm) {
                Bitboard unblockable_checks = unblockablechecks_bb(kntm_sq,pt);
                if (pt == PAWN) {
                    unavailable_squares = (unblockable_checks | square_bb(kntm_sq) | square_bb(ktm_sq)) & PawnSquaresBB;
                    n_available_squares = 48 - num_unblockablechecks(kntm_sq, PAWN) - bool(square_bb(kntm_sq) & PawnSquaresBB) - bool(square_bb(ktm_sq) & PawnSquaresBB);
                } else {
                    unavailable_squares = unblockable_checks | square_bb(kntm_sq) | square_bb(ktm_sq);
                    n_available_squares = 64 - num_unblockablechecks(kntm_sq, pt) - 2 + bool(square_bb(ktm_sq) & unblockable_checks);
                }
            } else {
                if (pt == PAWN) {
                    unavailable_squares = occupied_sqs & PawnSquaresBB; // stm pawns always occupy, kings may occupy PawnSquaresBB
                    n_available_squares = 48 - n_occupied_sqs + 2 - bool(square_bb(kntm_sq) & PawnSquaresBB) - bool(square_bb(ktm_sq) & PawnSquaresBB);
                } else {
                    unavailable_squares = occupied_sqs;
                    n_available_squares = 64 - n_occupied_sqs;
                }
            }

            std::cout << "ix_from_pos: " << PieceToChar[make_piece(c,pt)] << ": n_available_squares: " << n_available_squares;
            int piece_count = 0;
            int offset = (pt == PAWN) ? 8 : 0;
            while (transformedBB) {
                Square sq = pop_lsb(transformedBB);
                int k = popcount((square_bb(sq) - 1) & unavailable_squares); // count occupied squares lower than sq
                occupied_sqs |= square_bb(sq);
                n_occupied_sqs++;
                sqs_ixs[piece_count] = (int) sq - k - offset;
                std::cout << " " << square_to_uci(sq) << "->" << sqs_ixs[piece_count] << "(k:" << k << ")";
                piece_count++;
            }

            uint64_t tril_ix = tril_to_linear(piece_count, sqs_ixs);
            ix += tril_ix * multiplier;
            multiplier *= number_of_ordered_tuples(n_available_squares, piece_count);
            std::cout << " tril_ix: " << tril_ix << std::endl;
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
    Square ep_pawn_sq = SQ_NONE;
    Square ep_cap_pawn_sq = SQ_NONE;
    if (EP) {
        ix += num_nonep_pos;

        Square ep_sq = pos.ep_square() ^ flip;
        if (ep_sq & RightHalfBB) {
            flip ^= 7;
            ep_sq = ep_sq ^ 7;
        }
        is_horizontal_symmetric = false;

        ep_pawn_sq = ((pos.ep_square()) ^ flip) - NORTH;

        Bitboard pieceBB = pos.pieces(stm, PAWN);
        assert (pieceBB);
        // TODO: check maybe_update_flip_and_transform_bb always returns pieceBB?
        Bitboard transformed_bb = maybe_update_flip_and_transform_bb(pieceBB, is_horizontal_symmetric, flip) & attacks_bb<PAWN>(ep_sq, BLACK);
        ep_cap_pawn_sq = pop_lsb(transformed_bb);

        first_pawn = false;
        n_available_pawn_squares -= 2,
        n_available_squares -= 2;
        occupied_sqs[0] = ep_pawn_sq;
        occupied_sqs[1] = ep_cap_pawn_sq;
        n_occupied_sqs += 2;


        uint64_t ep_ix = EP_IX[ep_sq - SQ_A6][ep_cap_pawn_sq - ep_pawn_sq + 1];
        ix += ep_ix;
        multiplier *= 7;


        // std::cout << "ep_sq: " << square_to_uci(ep_sq) << ", ep_pawn_sq: " << square_to_uci(ep_pawn_sq) << ", ep_cap_pawn: " << square_to_uci(ep_cap_pawn_sq);
        // std::cout << ", i: "<< ep_sq - SQ_A6 << ", j: " << ep_cap_pawn_sq - ep_pawn_sq + 1 <<  ", ep_ix: " << ep_ix << std::endl;
    }

    for (Color c: {~stm, stm}) {
        Bitboard pieceBB = pos.pieces(c, PAWN);
        if (pieceBB) {
            Bitboard transformed_bb = maybe_update_flip_and_transform_bb(pieceBB, is_horizontal_symmetric, flip);
            if (EP) transformed_bb = transformed_bb & ~(square_bb(ep_pawn_sq) | square_bb(ep_cap_pawn_sq));
            int n_sq_ixs = 0;
            int piece_count = 0;
            while (transformed_bb) {
                Square sq = pop_lsb(transformed_bb);
                uint64_t k = insert_count_lt_squares(occupied_sqs, n_occupied_sqs, sq);

                if (first_pawn) {
                    first_pawn = false;
                    first_pawn_ix = (int) sq - k - 8;
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
    
    // std::cout << ix << " " << std::max(ix, num_nonep_pos) - num_nonep_pos << std::endl;

    return ix;
}

uint64_t ix_from_pos_(EGPosition const &pos, uint64_t num_nonep_pos, uint64_t num_ep_pos, const uint64_t kntm_poscounts[11]) {
    uint64_t ix = ix_from_pos_kkx(pos, kntm_poscounts);
    // assert (ix < num_nonep_pos + num_ep_pos);
    return ix;
}

void transform_to_canoncial(const EGPosition &pos, EGPosition &pos2) {
    Color stm = pos.side_to_move();
    bool no_pawns = pos.count<PAWN>(WHITE) + pos.count<PAWN>(BLACK) == 0;

    bool is_diag_symmetric = no_pawns;
    int8_t flip = 0;
    int8_t swap = 0;
    int8_t stm_flip = !no_pawns && (stm == BLACK) ? 56 : 0;

    Square orig_ktm_sq = pos.square<KING>(stm);
    Square orig_kntm_sq = pos.square<KING>(~stm);

    if (no_pawns) {
        flip = ((orig_kntm_sq & RightHalfBB) ? 7 : 0) ^ ((orig_kntm_sq & TopHalfBB) ? 56 : 0);
    } else {
        flip = ((orig_kntm_sq & RightHalfBB) ? 7 : 0) ^ stm_flip;
        // do not use stm == BLACK ? 56 : 0;
        // pos_at_ix flips back such that black pawns move south, internally a black pawn moving south is equivalent to a white pawn moving up
        if (pos.ep_square() != SQ_NONE) {
            Square ep_sq = pos.ep_square() ^ flip;
            if (ep_sq & RightHalfBB) {
                flip ^= 7;
                ep_sq = ep_sq ^ 7;
            }
            pos2.set_ep_square(ep_sq ^ stm_flip);
        }
    }

    Square ktnm_sq = maybe_update_swap_and_transform(orig_kntm_sq, flip, is_diag_symmetric, swap);
    pos2.put_piece(make_piece(~stm, KING), ktnm_sq ^ stm_flip);

    Square ktm_sq = maybe_update_swap_and_transform(orig_ktm_sq, flip, is_diag_symmetric, swap);
    pos2.put_piece(make_piece(stm, KING), ktm_sq ^ stm_flip);

    Color cs[10] = {stm, ~stm, stm, stm, stm, stm, ~stm, ~stm, ~stm, ~stm};
    PieceType pts[10] = {PAWN, PAWN, QUEEN, ROOK, BISHOP, KNIGHT, QUEEN, ROOK, BISHOP, KNIGHT};

    for (int i = 0; i < 10; i++) {
        Color c = cs[i];
        PieceType pt = pts[i];
        Bitboard bb = pos.pieces(c, pt);
        if (bb) {
            Bitboard transformedBB;
            transformedBB = maybe_update_swap_and_transform_bb(bb, flip, is_diag_symmetric, swap);
            while (transformedBB) {
                pos2.put_piece(make_piece(c,pt), pop_lsb(transformedBB) ^ stm_flip);
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