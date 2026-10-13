// Frozen copy of the original (v1) index and compressed file format.
// Used only by tools that read tables generated before the format/index changes (e.g. compare_tables).
// Do not modify: it has to stay identical to the code that produced the reference tables.

#include "legacy_v1.h"
#include <cstring>
#include <vector>

namespace v1 {

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


void pos_at_ix_(EGPosition &pos, uint64_t ix, Color stm, const int stm_pieces[6], const int sntm_pieces[6], const uint64_t kntm_poscounts[]) {
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


uint64_t ix_from_pos_(EGPosition const &pos, const uint64_t kntm_poscounts[]) {
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

// v1 file layout: checksum, num_pos, block_size (3 x uint64), block sizes (uint16 or uint32 if block_size > 65535),
// zstd-compressed int16 blocks
uint64_t compute_checksum(const int16_t* TB, uint64_t num_pos, [[maybe_unused]] int nthreads) {
    uint64_t s = 0;
    #pragma omp parallel for num_threads(nthreads) schedule(static) reduction(^:s)
    for (uint64_t ix = 0; ix < num_pos; ix++) {
        s ^= (ix << 10) | ((uint64_t) abs(TB[ix]));
    }
    return s;
}

bool load_compressed_table(const std::string& filename, uint64_t expected_num_pos, int16_t* TB, int nthreads) {
    FILE* f = fopen(filename.c_str(), "rb");
    if (f == NULL) return false;
    fseek(f, 0, SEEK_END);
    uint64_t filesize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(filesize);
    bool ok = fread(data.data(), 1, filesize, f) == filesize;
    fclose(f);
    if (!ok || filesize < 3 * sizeof(uint64_t)) return false;

    uint64_t header[3];
    memcpy(header, data.data(), sizeof(header));
    uint64_t checksum = header[0], num_pos = header[1], block_size = header[2];
    if (num_pos != expected_num_pos || block_size == 0) return false;

    uint64_t n_blocks = (num_pos + block_size - 1) / block_size;
    bool large_blocks = block_size > uint64_t(UINT16_MAX);
    uint64_t sizes_bytes = n_blocks * (large_blocks ? sizeof(uint32_t) : sizeof(uint16_t));
    if (filesize < sizeof(header) + sizes_bytes) return false;
    const uint8_t* sizes_ptr = data.data() + sizeof(header);
    std::vector<uint64_t> offsets(n_blocks + 1);
    offsets[0] = sizeof(header) + sizes_bytes;
    for (uint64_t b = 0; b < n_blocks; b++) {
        uint64_t s;
        if (large_blocks) { uint32_t x; memcpy(&x, sizes_ptr + 4 * b, 4); s = x; }
        else { uint16_t x; memcpy(&x, sizes_ptr + 2 * b, 2); s = x; }
        offsets[b + 1] = offsets[b] + s;
    }
    if (offsets[n_blocks] != filesize) return false;

    bool all_ok = true;
    #pragma omp parallel num_threads(nthreads) reduction(&&:all_ok)
    {
        ZSTD_DCtx* dctx = ZSTD_createDCtx();
        #pragma omp for schedule(static)
        for (uint64_t b = 0; b < n_blocks; b++) {
            uint64_t start_ix = b * block_size;
            uint64_t size = std::min(block_size, num_pos - start_ix) * sizeof(int16_t);
            size_t res = ZSTD_decompressDCtx(dctx, &TB[start_ix], size, data.data() + offsets[b], offsets[b + 1] - offsets[b]);
            if (ZSTD_isError(res) || res != size) all_ok = false;
        }
        ZSTD_freeDCtx(dctx);
    }
    return all_ok && compute_checksum(TB, num_pos, nthreads) == checksum;
}

} // namespace v1
