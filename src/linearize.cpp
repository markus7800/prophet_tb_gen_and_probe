#include "linearize.h"

void compute_poscounts(const int stm_pieces[6], const int sntm_pieces[6], uint64_t kntm_poscounts[], uint64_t& num_nonep_pos, uint64_t& num_ep_pos, uint64_t& num_pos) {
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
    
    num_nonep_pos = (n_pawns == 0) ? kntm_poscounts[N_KKX] : kntm_poscounts[N_KKP];

    if (stm_pieces[PAWN] == 0 || sntm_pieces[PAWN] == 0) {
        num_ep_pos = 0;
    } else {
        // EP adds ~1% could be improved however
        uint64_t n = N_EP * N_KKP;

        // pawns
        uint64_t n_squares_availables = 48 - 2; // sub two ep pawns
        n *= number_of_ordered_tuples(n_squares_availables, stm_pieces[PAWN]-1);

        n_squares_availables = 48 - 1 - stm_pieces[PAWN]; // sub sntm ep pawn and stm pawns
        n *= number_of_ordered_tuples(n_squares_availables, sntm_pieces[PAWN]-1);

        // pieces
        n_squares_availables = 64 - 2 - stm_pieces[PAWN] - sntm_pieces[PAWN]; // sub kings and pawns

        for (int stm = 1; stm >= 0; --stm) {
            const int* pieces = (stm) ? stm_pieces : sntm_pieces;
            for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) {
                n *= number_of_ordered_tuples(n_squares_availables, pieces[pt]);
                n_squares_availables -= pieces[pt];
            }
        }

        num_ep_pos = n;
    }

    num_pos = num_nonep_pos + num_ep_pos;
}


static void pos_at_ix_unsplit(EGPosition &pos, uint64_t ix, Color stm, const int stm_pieces[6], const int sntm_pieces[6], const uint64_t kntm_poscounts[]) {
    bool no_pawns = (stm_pieces[PAWN] + sntm_pieces[PAWN] == 0);
    pos.set_side_to_move(stm);
    int8_t bptm_flip = !no_pawns && (stm == BLACK) ? 56 : 0; // black pawn to move flip

    uint64_t s = 0;
    Bitboard available_squares = 0;
    uint64_t n_available_squares = 0;

    bool EP = !no_pawns && (kntm_poscounts[N_KKP] <= ix); // kntm_poscounts[N_KKP] == number of non-ep positions
    int kix;

    Bitboard occupied_sqs = 0; // not bptm_flipped
    int n_occupied_sqs = 0;

    if (EP) {
        ix -= kntm_poscounts[N_KKP];
        uint64_t ep_ix = ix % N_EP;
        ix = ix / N_EP;
        Square ep_pawn = EP_PAWN[ep_ix];
        Square ep_cap_pawn = EP_CAP_PAWN[ep_ix];
        pos.put_piece(make_piece(~stm, PAWN), ep_pawn ^ bptm_flip);
        pos.put_piece(make_piece(stm, PAWN), ep_cap_pawn ^ bptm_flip);
        pos.set_ep_square((EP_PAWN[ep_ix] + NORTH) ^ bptm_flip);

        kix = ix % N_KKP;
        ix = ix / N_KKP; // may be placed on top of ep pawns
        // std::cout << "pos_at_ix: ep_ix: " << ep_ix << " kix: " << kix << std::endl;

        occupied_sqs |= (square_bb(ep_pawn) | square_bb(ep_cap_pawn));
        n_occupied_sqs += 2;

    } else {

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

        kix = l-1;
        ix -= kntm_poscounts[kix];

        // assert (kntm_poscounts[kix] <= ix && ix < kntm_poscounts[kix+1]);
    }


    Square kntm_sq = no_pawns ? Square(KKX_KNTM_SQ[kix]) : Square(KKP_KNTM_SQ[kix]);
    Square ktm_sq = no_pawns ? Square(KKX_KTM_SQ[kix]) : Square(KKP_KTM_SQ[kix]);
    // std::cout << "pos_at_ix: " << "kix: " << kix << " kntm_sq: " << square_to_uci(kntm_sq) << " ktm_sq: " << square_to_uci(ktm_sq) << std::endl;

    pos.put_piece(make_piece(stm, KING), ktm_sq ^ bptm_flip);
    pos.put_piece(make_piece(~stm, KING), kntm_sq ^ bptm_flip);

    if (!EP) {
        occupied_sqs |= (square_bb(ktm_sq) | square_bb(kntm_sq));
        n_occupied_sqs += 2;
    } // otherwise we add after pawns

    int sqs_ixs[4];

    Color cs[10] = {stm, ~stm, stm, stm, stm, stm, ~stm, ~stm, ~stm, ~stm};
    PieceType pts[10] = {PAWN, PAWN, QUEEN, ROOK, BISHOP, KNIGHT, QUEEN, ROOK, BISHOP, KNIGHT};

    for (int i = 0; i < 10; i++) {
        Color c = cs[i];
        PieceType pt = pts[i];

        if (EP && c == stm && pt == QUEEN) {
            // finished with pawns, now we add kings to occupied squares
            occupied_sqs |= (square_bb(ktm_sq) | square_bb(kntm_sq));
            n_occupied_sqs += 2;
        }

        const int* c_pieces = (c == stm) ? stm_pieces : sntm_pieces;
        int piece_count = c_pieces[pt] - (EP && (pt == PAWN));  // if EP already placed one pawn for each side
        if (piece_count == 0) { continue; }
        Piece pc = make_piece(c, pt);

        if (EP) {
            if (pt == PAWN) {
                available_squares = ~(occupied_sqs | Rank1BB | Rank8BB); // occupied_sqs only contains pawns (ep + potentially stm pawns) at this point
                n_available_squares = 48 - n_occupied_sqs;
            } else {
                available_squares = ~occupied_sqs;
                n_available_squares = 64 - n_occupied_sqs;
            }
        } else if (c == stm) {
            Bitboard unblockable_checks = unblockablechecks_bb(kntm_sq,pt);
            if (pt == PAWN) {
                available_squares = ~(unblockable_checks | square_bb(kntm_sq) | square_bb(ktm_sq) | Rank1BB | Rank8BB); // adding Rank1BB and Rank8BB handles offset in nth_set_sq
                n_available_squares = 48 - num_unblockablechecks(kntm_sq, PAWN) - bool(square_bb(kntm_sq) & PawnSquaresBB) - bool(square_bb(ktm_sq) & PawnSquaresBB);
            } else {
                available_squares = ~(unblockable_checks | square_bb(kntm_sq) | square_bb(ktm_sq));
                n_available_squares = 64 - num_unblockablechecks(kntm_sq, pt) - 2 + bool(square_bb(ktm_sq) & unblockable_checks);
            }
        } else {
            if (pt == PAWN) {
                available_squares = ~occupied_sqs & PawnSquaresBB; // stm pawns always occupy, kings may occupy PawnSquaresBB
                n_available_squares = 48 - n_occupied_sqs + 2 - bool(square_bb(kntm_sq) & PawnSquaresBB) - bool(square_bb(ktm_sq) & PawnSquaresBB);
            } else {
                available_squares = ~occupied_sqs;
                n_available_squares = 64 - n_occupied_sqs;
            }
        }

        s = number_of_ordered_tuples(n_available_squares, piece_count);
        uint64_t tril_ix = ix % s;
        ix = ix / s;
        
        // std::cout << "pos_at_ix: " << PieceToChar[pc] << ": n_available_squares: " << n_available_squares << " tril_ix: " << tril_ix << " piece_count: " << piece_count << std::endl;
    
        tril_from_linear(piece_count, tril_ix, sqs_ixs);

        for (int j = 0; j < piece_count; j++) {
            Square sq = nth_set_sq(available_squares, sqs_ixs[j]);
            // std::cout << " " << sqs_ixs[j] << "->" << square_to_uci(sq ^ bptm_flip);
            pos.put_piece(pc, sq ^ bptm_flip);
            occupied_sqs |= square_bb(sq);
            n_occupied_sqs++;
        }
        // std::cout << std::endl;
    }
    
    if (n_occupied_sqs > 6) { std::cout << "More than 6 pieces not supported! (have " << n_occupied_sqs << ")\n"; assert(false); }

    // can be broken such that popcount(pos.pieces()) != n_occupied_sqs
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


static uint64_t ix_from_pos_unsplit(EGPosition const &pos, const uint64_t kntm_poscounts[]) {
    bool no_pawns = (pos.count<PAWN>(WHITE) + pos.count<PAWN>(BLACK) == 0);

    Color stm = pos.side_to_move();

    bool is_diag_symmetric = no_pawns; // if we have pawns we disable swapping along diagonal

    int n_occupied_sqs = 0;
    Bitboard occupied_sqs = 0; // transformed squares

    Square orig_kntm_sq = pos.square<KING>(~stm);
    Square orig_ktm_sq = pos.square<KING>(stm);

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

    uint64_t ix = 0;
    uint64_t multiplier = 1;

    bool EP = pos.ep_square() != SQ_NONE;
    Square ep_pawn_sq = SQ_NONE;
    Square ep_cap_pawn_sq = SQ_NONE;
    if (EP) {
        ix += kntm_poscounts[N_KKP];  // kntm_poscounts[N_KKP] == number of non-ep positions

        Square ep_sq = pos.ep_square() ^ flip;
        assert (square_bb(ep_sq) & Rank6BB);
        ep_pawn_sq = ep_sq - NORTH;

        // maybe_update_swap_and_transform_bb only flips
        Bitboard ep_cap_pawns = maybe_update_swap_and_transform_bb(pos.pieces(stm, PAWN), flip, is_diag_symmetric, swap) & attacks_bb<PAWN>(ep_sq, BLACK);
        ep_cap_pawn_sq = pop_lsb(ep_cap_pawns); // take left-most pawn that can do ep

        // ep_sq, ep_pawn_sq, and ep_cap_pawn_sq are transformed

        occupied_sqs |= (square_bb(ep_pawn_sq) | square_bb(ep_cap_pawn_sq));
        n_occupied_sqs += 2;

        // ep_sq - SQ_A6 maps ep_sq to 0 ... 7
        // ep_cap_pawn_sq and ep_pawn_sq are adjacent on same rank
        // ep_cap_pawn_sq - ep_pawn_sq is -1 or +1
        uint64_t ep_ix = EP_IX[ep_sq - SQ_A6][ep_cap_pawn_sq - ep_pawn_sq + 1];
        ix += ep_ix;
        multiplier *= N_EP;

        // std::cout << "ix_from_pos: ep_ix: " << ep_ix;
        // std::cout << " ep_sq: " << square_to_uci(ep_sq) << ", ep_pawn_sq: " << square_to_uci(ep_pawn_sq) << ", ep_cap_pawn: " << square_to_uci(ep_cap_pawn_sq);
        // std::cout << ", i: "<< ep_sq - SQ_A6 << ", j: " << ep_cap_pawn_sq - ep_pawn_sq + 1 <<  ", ep_ix: " << ep_ix << std::endl;

        assert(EP_PAWN[ep_ix] == ep_pawn_sq);
        assert(EP_CAP_PAWN[ep_ix] == ep_cap_pawn_sq);
    }

    Square kntm_sq = maybe_update_swap_and_transform(orig_kntm_sq, flip, is_diag_symmetric, swap);

    Square ktm_sq = maybe_update_swap_and_transform(orig_ktm_sq, flip, is_diag_symmetric, swap);
    

    if (!EP) {
        occupied_sqs |= (square_bb(kntm_sq) | square_bb(ktm_sq));
        n_occupied_sqs += 2;
    } // otherwise we add after pawns

    int16_t kix = no_pawns ? get_kkx_ix(kntm_sq, ktm_sq) : get_kkp_ix(kntm_sq, ktm_sq);
    if (EP) {
        ix += kix * multiplier;
        multiplier *= N_KKP;
        // std::cout << " kix: " << kix << std::endl;
    } else {
        ix += kntm_poscounts[kix];
    }

    // std::cout << "ix_from_pos: " << "kix: " << kix << " kntm_sq: " << square_to_uci(kntm_sq) << " ktm_sq: " << square_to_uci(ktm_sq) << std::endl;

    int sqs_ixs[4];
    Bitboard unavailable_squares;
    uint64_t n_available_squares;

    Color cs[10] = {stm, ~stm, stm, stm, stm, stm, ~stm, ~stm, ~stm, ~stm};
    PieceType pts[10] = {PAWN, PAWN, QUEEN, ROOK, BISHOP, KNIGHT, QUEEN, ROOK, BISHOP, KNIGHT};
    
    for (int i = 0; i < 10; i++) {
        Color c = cs[i];
        PieceType pt = pts[i];

        if (EP && c == stm && pt == QUEEN) {
            // finished with pawns, now we add kings to occupied squares
            occupied_sqs |= (square_bb(kntm_sq) | square_bb(ktm_sq));
            n_occupied_sqs += 2;
        }

        Bitboard pieceBB = pos.pieces(c, pt);
        if (pieceBB) {
            Bitboard transformedBB = maybe_update_swap_and_transform_bb(pieceBB, flip, is_diag_symmetric, swap);

            if (EP) {
                if (pt == PAWN) {
                    // already accounted for ep pawns
                    transformedBB = transformedBB & ~(square_bb(ep_pawn_sq) | square_bb(ep_cap_pawn_sq));

                    unavailable_squares = occupied_sqs & PawnSquaresBB; // occupied_sqs only contains pawns (ep + potentially stm pawns) at this point
                    n_available_squares = 48 - n_occupied_sqs;
                } else {
                    unavailable_squares = occupied_sqs;
                    n_available_squares = 64 - n_occupied_sqs;
                }

            } else if (c == stm) {
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

            // std::cout << "ix_from_pos: " << PieceToChar[make_piece(c,pt)] << ": n_available_squares: " << n_available_squares;
            int piece_count = 0;
            int offset = (pt == PAWN) ? 8 : 0;
            while (transformedBB) {
                Square sq = pop_lsb(transformedBB);
                int k = popcount((square_bb(sq) - 1) & unavailable_squares); // count occupied squares lower than sq
                occupied_sqs |= square_bb(sq);
                n_occupied_sqs++;
                sqs_ixs[piece_count] = (int) sq - k - offset;
                // std::cout << " " << square_to_uci(sq) << "->" << sqs_ixs[piece_count] << "(k:" << k << ")";
                piece_count++;
            }

            uint64_t tril_ix = tril_to_linear(piece_count, sqs_ixs);
            ix += tril_ix * multiplier;
            multiplier *= number_of_ordered_tuples(n_available_squares, piece_count);
            // std::cout << " tril_ix: " << tril_ix << " piece_count: " << piece_count << std::endl;
        }
    }

    return ix;
}


void transform_to_canoncial(const EGPosition &pos, EGPosition &pos2) {
    Color stm = pos.side_to_move();
    bool no_pawns = pos.count<PAWN>(WHITE) + pos.count<PAWN>(BLACK) == 0;

    bool is_diag_symmetric = no_pawns;
    int8_t flip = 0;
    int8_t swap = 0;
    int8_t bptm_flip = !no_pawns && (stm == BLACK) ? 56 : 0;

    Square orig_ktm_sq = pos.square<KING>(stm);
    Square orig_kntm_sq = pos.square<KING>(~stm);

    if (no_pawns) {
        flip = ((orig_kntm_sq & RightHalfBB) ? 7 : 0) ^ ((orig_kntm_sq & TopHalfBB) ? 56 : 0);
    } else {
        flip = ((orig_kntm_sq & RightHalfBB) ? 7 : 0) ^ bptm_flip;
        // pos_at_ix flips back such that black pawns move south, internally a black pawn moving south is equivalent to a white pawn moving up
        if (pos.ep_square() != SQ_NONE) {
            Square ep_sq = pos.ep_square() ^ flip;
            pos2.set_ep_square(ep_sq ^ bptm_flip);
        }
    }

    Square ktnm_sq = maybe_update_swap_and_transform(orig_kntm_sq, flip, is_diag_symmetric, swap);
    pos2.put_piece(make_piece(~stm, KING), ktnm_sq ^ bptm_flip);

    Square ktm_sq = maybe_update_swap_and_transform(orig_ktm_sq, flip, is_diag_symmetric, swap);
    pos2.put_piece(make_piece(stm, KING), ktm_sq ^ bptm_flip);

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
                pos2.put_piece(make_piece(c,pt), pop_lsb(transformedBB) ^ bptm_flip);
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
    if (pos.ep_square() != SQ_NONE)
        pos2.set_ep_square(transform(pos.ep_square(), h_flip ^ v_flip, swap));

    pos2.set_side_to_move(pos.side_to_move());

}

Bitboard transform_preimage_bb(Bitboard target, int8_t h_flip, int8_t v_flip, int8_t swap) {
    Bitboard preimage = 0;
    for (Square sq = SQ_A1; sq <= SQ_H8; ++sq) {
        if (target & transform(sq, h_flip ^ v_flip, swap)) preimage |= square_bb(sq);
    }
    return preimage;
}

// ---- tables split into parts by pawn file classes

bool is_split_table(const int stm_pieces[6], const int sntm_pieces[6]) {
    int n_pawns = stm_pieces[PAWN] + sntm_pieces[PAWN];
    return n_pawns == 1 || n_pawns == 2;
}

// all multisets of k file classes (k <= 2) as class counts, in a fixed order
static std::vector<std::vector<int>> class_multisets(int k) {
    std::vector<std::vector<int>> result;
    if (k == 0) {
        result.push_back({0, 0, 0, 0});
    } else if (k == 1) {
        for (int c = 0; c < 4; c++) { std::vector<int> m(4, 0); m[c] = 1; result.push_back(m); }
    } else {
        assert (k == 2);
        for (int c1 = 0; c1 < 4; c1++) {
            for (int c2 = c1; c2 < 4; c2++) { std::vector<int> m(4, 0); m[c1]++; m[c2]++; result.push_back(m); }
        }
    }
    return result;
}

static Bitboard flip_bb(Bitboard b, int8_t flip) {
    Bitboard r = 0;
    while (b) r |= square_bb(Square(pop_lsb(b) ^ flip));
    return r;
}

// product of the radices of the non-pawn pieces in the non-ep section of a king pair slice (same as compute_poscounts)
static uint64_t pieces_radix(const int stm_pieces[6], const int sntm_pieces[6], Square kntm_sq, Square ktm_sq) {
    uint64_t n = 1;
    uint64_t n_squares_available_to_sntm = 62 - stm_pieces[PAWN] - sntm_pieces[PAWN];
    for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) {
        uint64_t n_domain = 64 - num_unblockablechecks(kntm_sq, pt) - 2 + bool(square_bb(ktm_sq) & unblockablechecks_bb(kntm_sq, pt));
        n *= number_of_ordered_tuples(n_domain, stm_pieces[pt]);
        n_squares_available_to_sntm -= stm_pieces[pt];
    }
    for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) {
        n *= number_of_ordered_tuples(n_squares_available_to_sntm, sntm_pieces[pt]);
        n_squares_available_to_sntm -= sntm_pieces[pt];
    }
    return n;
}


void build_table_index(const int stm_pieces[6], const int sntm_pieces[6], TableIndex& t) {
    for (int i = 0; i < 6; i++) {
        t.stm_pieces[i] = stm_pieces[i];
        t.sntm_pieces[i] = sntm_pieces[i];
    }
    int stm_pawns = stm_pieces[PAWN];
    int sntm_pawns = sntm_pieces[PAWN];
    t.no_pawns = stm_pawns + sntm_pawns == 0;
    t.split = is_split_table(stm_pieces, sntm_pieces);
    t.n_kk = t.no_pawns ? N_KKX : N_KKP;

    if (!t.split) {
        t.n_parts = 1;
        t.poscounts.assign(N_KKP + 1, 0);
        compute_poscounts(stm_pieces, sntm_pieces, t.poscounts.data(), t.num_nonep_pos, t.num_ep_pos, t.num_pos);
        t.poscounts.resize(t.n_kk + 1);
        PartInfo& P = t.parts[0];
        for (int c = 0; c < 4; c++) { P.stm_class_count[c] = 0; P.sntm_class_count[c] = 0; }
        P.start = 0;
        P.nonep_size = t.num_nonep_pos;
        P.ep_size = t.num_ep_pos;
        P.n_ep = t.num_ep_pos > 0 ? N_EP : 0;
        for (int e = 0; e < N_EP; e++) { P.ep_local[e] = e; P.ep_global[e] = e; }
        return;
    }

    std::vector<std::vector<int>> stm_ms = class_multisets(stm_pawns);
    std::vector<std::vector<int>> sntm_ms = class_multisets(sntm_pawns);
    t.n_parts = stm_ms.size() * sntm_ms.size();
    assert (t.n_parts <= MAX_PARTS);
    t.poscounts.assign((uint64_t) t.n_parts * (N_KKP + 1), 0);

    std::vector<uint64_t> pieces_radices(N_KKP);
    for (int kix = 0; kix < N_KKP; kix++) {
        pieces_radices[kix] = pieces_radix(stm_pieces, sntm_pieces, Square(KKP_KNTM_SQ[kix]), Square(KKP_KTM_SQ[kix]));
    }
    // en-passant section: pieces are placed on any free square (64 - kings - 2 pawns), as for unsplit tables
    uint64_t ep_pieces_radix = 1;
    {
        uint64_t n_available = 64 - 2 - stm_pawns - sntm_pawns;
        for (int stm = 1; stm >= 0; --stm) {
            const int* pieces = stm ? stm_pieces : sntm_pieces;
            for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) {
                ep_pieces_radix *= number_of_ordered_tuples(n_available, pieces[pt]);
                n_available -= pieces[pt];
            }
        }
    }

    uint64_t start = 0;
    t.num_nonep_pos = 0;
    t.num_ep_pos = 0;
    for (uint64_t i = 0; i < stm_ms.size(); i++) {
        for (uint64_t j = 0; j < sntm_ms.size(); j++) {
            int p = i * sntm_ms.size() + j;
            PartInfo& P = t.parts[p];
            for (int c = 0; c < 4; c++) { P.stm_class_count[c] = stm_ms[i][c]; P.sntm_class_count[c] = sntm_ms[j][c]; }

            uint64_t* pc = &t.poscounts[(uint64_t) p * (N_KKP + 1)];
            pc[0] = 0;
            for (int kix = 0; kix < N_KKP; kix++) {
                Square kntm_sq = Square(KKP_KNTM_SQ[kix]);
                Square ktm_sq = Square(KKP_KTM_SQ[kix]);
                Bitboard kings = square_bb(kntm_sq) | square_bb(ktm_sq);
                uint64_t n = pieces_radices[kix];
                for (int c = 0; c < 4; c++) {
                    n *= number_of_ordered_tuples(popcount(stm_pawn_class_domain(c, kntm_sq, ktm_sq)), P.stm_class_count[c]);
                }
                for (int c = 0; c < 4; c++) {
                    // the stm pawns of class c are on the files of c, the others do not matter
                    uint64_t n_domain = popcount(PawnSquaresBB & class_files_bb(c) & ~kings) - P.stm_class_count[c];
                    n *= number_of_ordered_tuples(n_domain, P.sntm_class_count[c]);
                }
                pc[kix + 1] = pc[kix] + n;
            }

            P.start = start;
            P.nonep_size = pc[N_KKP];
            P.n_ep = 0;
            for (int e = 0; e < N_EP; e++) P.ep_local[e] = -1;
            if (stm_pawns == 1 && sntm_pawns == 1) {
                for (int e = 0; e < N_EP; e++) {
                    if (P.stm_class_count[file_class(EP_CAP_PAWN[e])] == 1 && P.sntm_class_count[file_class(EP_PAWN[e])] == 1) {
                        P.ep_local[e] = P.n_ep;
                        P.ep_global[P.n_ep] = e;
                        P.n_ep++;
                    }
                }
            }
            P.ep_size = (uint64_t) P.n_ep * N_KKP * ep_pieces_radix;
            start += P.nonep_size + P.ep_size;
            t.num_nonep_pos += P.nonep_size;
            t.num_ep_pos += P.ep_size;
        }
    }
    t.num_pos = start;
}

int TableIndex::part_of_class_counts(const int stm_class_count[4], const int sntm_class_count[4]) const {
    if (!split) return -1;
    for (int p = 0; p < n_parts; p++) {
        bool equal = true;
        for (int c = 0; c < 4; c++) {
            equal = equal && parts[p].stm_class_count[c] == stm_class_count[c] && parts[p].sntm_class_count[c] == sntm_class_count[c];
        }
        if (equal) return p;
    }
    return -1;
}

bool promotion_compatible(const TableIndex& before, int before_part, const TableIndex& after, int after_part) {
    if (!before.split || !after.split) return true;
    const PartInfo& B = before.parts[before_part];
    const PartInfo& A = after.parts[after_part];
    // the pawns of the side that does not promote are unchanged (stm after the promotion)
    for (int c = 0; c < 4; c++) {
        if (A.stm_class_count[c] != B.sntm_class_count[c]) return false;
    }
    // the promoting side loses one pawn of some class
    for (int promoted = 0; promoted < 4; promoted++) {
        if (B.stm_class_count[promoted] == 0) continue;
        bool equal = true;
        for (int c = 0; c < 4; c++) {
            equal = equal && A.sntm_class_count[c] == B.stm_class_count[c] - (c == promoted);
        }
        if (equal) return true;
    }
    return false;
}

static void pos_at_ix_split(EGPosition &pos, uint64_t ix, Color stm, const TableIndex& t) {
    int p = t.part_of(ix);
    const PartInfo& P = t.parts[p];
    uint64_t local = ix - P.start;
    int8_t flip = (stm == BLACK) ? 56 : 0; // black pawn to move flip
    pos.set_side_to_move(stm);

    Bitboard occupied = 0; // not flipped
    int n_occupied = 0;
    int sqs_ixs[8];

    if (local >= P.nonep_size) {
        // en-passant: ep pawns, kings, then pieces on any free square
        local -= P.nonep_size;
        int e = P.ep_global[local % P.n_ep];
        local /= P.n_ep;
        int kix = local % N_KKP;
        local /= N_KKP;
        pos.put_piece(make_piece(~stm, PAWN), Square(EP_PAWN[e] ^ flip));
        pos.put_piece(make_piece(stm, PAWN), Square(EP_CAP_PAWN[e] ^ flip));
        pos.set_ep_square(Square((EP_PAWN[e] + NORTH) ^ flip));
        Square kntm_sq = Square(KKP_KNTM_SQ[kix]);
        Square ktm_sq = Square(KKP_KTM_SQ[kix]);
        pos.put_piece(make_piece(stm, KING), Square(ktm_sq ^ flip));
        pos.put_piece(make_piece(~stm, KING), Square(kntm_sq ^ flip));
        occupied = square_bb(EP_PAWN[e]) | square_bb(EP_CAP_PAWN[e]) | square_bb(kntm_sq) | square_bb(ktm_sq);
        n_occupied = 4;
        for (int side = 1; side >= 0; --side) {
            Color c = side ? stm : ~stm;
            const int* pieces = side ? t.stm_pieces : t.sntm_pieces;
            for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) {
                int count = pieces[pt];
                if (count == 0) continue;
                Bitboard available = ~occupied;
                uint64_t s = number_of_ordered_tuples(64 - n_occupied, count);
                uint64_t tril_ix = local % s;
                local /= s;
                tril_from_linear(count, tril_ix, sqs_ixs);
                for (int j = 0; j < count; j++) {
                    Square sq = nth_set_sq(available, sqs_ixs[j]);
                    pos.put_piece(make_piece(c, pt), Square(sq ^ flip));
                    occupied |= square_bb(sq);
                    n_occupied++;
                }
            }
        }
        return;
    }

    // king pair slice
    const uint64_t* pc = t.part_poscounts(p);
    int l = 0, r = N_KKP;
    while (l < r) {
        int m = l + (r - l) / 2;
        if (pc[m] <= local) l = m + 1; else r = m;
    }
    int kix = l - 1;
    local -= pc[kix];

    Square kntm_sq = Square(KKP_KNTM_SQ[kix]);
    Square ktm_sq = Square(KKP_KTM_SQ[kix]);
    pos.put_piece(make_piece(stm, KING), Square(ktm_sq ^ flip));
    pos.put_piece(make_piece(~stm, KING), Square(kntm_sq ^ flip));
    occupied = square_bb(kntm_sq) | square_bb(ktm_sq);
    n_occupied = 2;

    // pawns by class: stm pawns (classes ascending), then sntm pawns (classes ascending)
    for (int side = 1; side >= 0; --side) {
        Color c = side ? stm : ~stm;
        const int* class_count = side ? P.stm_class_count : P.sntm_class_count;
        Bitboard occupied_before = occupied;
        for (int cl = 0; cl < 4; cl++) {
            int count = class_count[cl];
            if (count == 0) continue;
            Bitboard domain = side ? stm_pawn_class_domain(cl, kntm_sq, ktm_sq) : sntm_pawn_class_domain(cl, occupied_before);
            uint64_t s = number_of_ordered_tuples(popcount(domain), count);
            uint64_t tril_ix = local % s;
            local /= s;
            tril_from_linear(count, tril_ix, sqs_ixs);
            for (int j = 0; j < count; j++) {
                Square sq = nth_set_sq(domain, sqs_ixs[j]);
                pos.put_piece(make_piece(c, PAWN), Square(sq ^ flip));
                occupied |= square_bb(sq);
                n_occupied++;
            }
        }
    }

    // pieces as for unsplit tables
    for (int side = 1; side >= 0; --side) {
        Color c = side ? stm : ~stm;
        const int* pieces = side ? t.stm_pieces : t.sntm_pieces;
        for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) {
            int count = pieces[pt];
            if (count == 0) continue;
            Bitboard available;
            uint64_t n_available;
            if (side) {
                Bitboard unblockable_checks = unblockablechecks_bb(kntm_sq, pt);
                available = ~(unblockable_checks | square_bb(kntm_sq) | square_bb(ktm_sq));
                n_available = 64 - num_unblockablechecks(kntm_sq, pt) - 2 + bool(square_bb(ktm_sq) & unblockable_checks);
            } else {
                available = ~occupied;
                n_available = 64 - n_occupied;
            }
            uint64_t s = number_of_ordered_tuples(n_available, count);
            uint64_t tril_ix = local % s;
            local /= s;
            tril_from_linear(count, tril_ix, sqs_ixs);
            for (int j = 0; j < count; j++) {
                Square sq = nth_set_sq(available, sqs_ixs[j]);
                pos.put_piece(make_piece(c, pt), Square(sq ^ flip));
                occupied |= square_bb(sq);
                n_occupied++;
            }
        }
    }
}

static uint64_t ix_from_pos_split(EGPosition const &pos, const TableIndex& t) {
    Color stm = pos.side_to_move();
    Square orig_kntm_sq = pos.square<KING>(~stm);
    Square orig_ktm_sq = pos.square<KING>(stm);
    int8_t flip = ((orig_kntm_sq & RightHalfBB) ? 7 : 0) ^ (stm == BLACK ? 56 : 0);
    Square kntm_sq = Square(orig_kntm_sq ^ flip);
    Square ktm_sq = Square(orig_ktm_sq ^ flip);
    Bitboard kings = square_bb(kntm_sq) | square_bb(ktm_sq);
    int kix = get_kkp_ix(kntm_sq, ktm_sq);

    Bitboard stm_pawns = flip_bb(pos.pieces(stm, PAWN), flip);
    Bitboard sntm_pawns = flip_bb(pos.pieces(~stm, PAWN), flip);
    int stm_class_count[4] = {0, 0, 0, 0};
    int sntm_class_count[4] = {0, 0, 0, 0};
    for (Bitboard b = stm_pawns; b; ) stm_class_count[file_class(pop_lsb(b))]++;
    for (Bitboard b = sntm_pawns; b; ) sntm_class_count[file_class(pop_lsb(b))]++;
    int p = t.part_of_class_counts(stm_class_count, sntm_class_count);
    assert (p >= 0);
    const PartInfo& P = t.parts[p];

    uint64_t ix = 0;
    uint64_t multiplier = 1;
    int sqs_ixs[8];

    if (pos.ep_square() != SQ_NONE) {
        Square ep_sq = Square(pos.ep_square() ^ flip);
        Square ep_pawn_sq = ep_sq - NORTH;
        Bitboard ep_cap_pawns = stm_pawns & attacks_bb<PAWN>(ep_sq, BLACK);
        Square ep_cap_pawn_sq = lsb(ep_cap_pawns);
        int e = EP_IX[ep_sq - SQ_A6][ep_cap_pawn_sq - ep_pawn_sq + 1];
        assert (P.ep_local[e] >= 0);
        ix = P.ep_local[e];
        multiplier = P.n_ep;
        ix += kix * multiplier;
        multiplier *= N_KKP;

        Bitboard occupied = square_bb(ep_pawn_sq) | square_bb(ep_cap_pawn_sq) | kings;
        int n_occupied = 4;
        for (int side = 1; side >= 0; --side) {
            Color c = side ? stm : ~stm;
            for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) {
                Bitboard bb = flip_bb(pos.pieces(c, pt), flip);
                if (!bb) continue;
                int count = 0;
                Bitboard unavailable = occupied;
                while (bb) {
                    Square sq = pop_lsb(bb);
                    sqs_ixs[count++] = (int) sq - popcount((square_bb(sq) - 1) & unavailable);
                    occupied |= square_bb(sq);
                }
                ix += tril_to_linear(count, sqs_ixs) * multiplier;
                multiplier *= number_of_ordered_tuples(64 - n_occupied, count);
                n_occupied += count;
            }
        }
        return P.start + P.nonep_size + ix;
    }

    // pawns by class: stm pawns (classes ascending), then sntm pawns (classes ascending)
    Bitboard occupied = kings;
    for (int side = 1; side >= 0; --side) {
        Bitboard pawns = side ? stm_pawns : sntm_pawns;
        Bitboard occupied_before = occupied;
        for (int cl = 0; cl < 4; cl++) {
            Bitboard bb = pawns & class_files_bb(cl);
            if (!bb) continue;
            Bitboard domain = side ? stm_pawn_class_domain(cl, kntm_sq, ktm_sq) : sntm_pawn_class_domain(cl, occupied_before);
            int count = 0;
            while (bb) {
                Square sq = pop_lsb(bb);
                sqs_ixs[count++] = popcount(domain & (square_bb(sq) - 1));
            }
            ix += tril_to_linear(count, sqs_ixs) * multiplier;
            multiplier *= number_of_ordered_tuples(popcount(domain), count);
        }
        occupied |= pawns;
    }
    int n_occupied = 2 + popcount(stm_pawns) + popcount(sntm_pawns);

    // pieces as for unsplit tables
    for (int side = 1; side >= 0; --side) {
        Color c = side ? stm : ~stm;
        for (PieceType pt = QUEEN; pt >= KNIGHT; --pt) {
            Bitboard bb = flip_bb(pos.pieces(c, pt), flip);
            if (!bb) continue;
            Bitboard unavailable;
            uint64_t n_available;
            if (side) {
                Bitboard unblockable_checks = unblockablechecks_bb(kntm_sq, pt);
                unavailable = unblockable_checks | kings;
                n_available = 64 - num_unblockablechecks(kntm_sq, pt) - 2 + bool(square_bb(ktm_sq) & unblockable_checks);
            } else {
                unavailable = occupied;
                n_available = 64 - n_occupied;
            }
            int count = 0;
            while (bb) {
                Square sq = pop_lsb(bb);
                sqs_ixs[count++] = (int) sq - popcount((square_bb(sq) - 1) & unavailable);
                occupied |= square_bb(sq);
            }
            ix += tril_to_linear(count, sqs_ixs) * multiplier;
            multiplier *= number_of_ordered_tuples(n_available, count);
            n_occupied += count;
        }
    }
    return P.start + t.part_poscounts(p)[kix] + ix;
}

void pos_at_ix_(EGPosition &pos, uint64_t ix, Color stm, const TableIndex& t) {
    if (t.split) pos_at_ix_split(pos, ix, stm, t);
    else pos_at_ix_unsplit(pos, ix, stm, t.stm_pieces, t.sntm_pieces, t.poscounts.data());
}

uint64_t ix_from_pos_(EGPosition const &pos, const TableIndex& t) {
    if (t.split) return ix_from_pos_split(pos, t);
    return ix_from_pos_unsplit(pos, t.poscounts.data());
}
