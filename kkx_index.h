
#ifndef KKX_INDEX_H_INCLUDED
#define KKX_INDEX_H_INCLUDED

#include <vector>

#include "kkx.h"
#include "eg_position.h"
#include "uci.h"

class KKXIndex {
private:
    // have to be sorted descending, no pawns, no duplicate pieces per side
    std::vector<PieceType> stm_pieces;
    std::vector<PieceType> sntm_pieces;
public:
    KKXIndex(std::vector<PieceType>& stm_pieces, std::vector<PieceType>& sntm_pieces) {
        this->stm_pieces = stm_pieces;
        this->sntm_pieces = sntm_pieces;
    }

    int n_pieces() const;
    uint64_t num_positions() const;

    void pos_at_ix(EGPosition &pos, uint64_t ix, Color stm) const;
    uint64_t ix_from_pos(EGPosition &pos) const;
};

int KKXIndex::n_pieces() const {
    return stm_pieces.size() + sntm_pieces.size();
}

uint64_t KKXIndex::num_positions() const {
    uint64_t n = N_KKX;
    int k = n_pieces();
    uint64_t s = 62;
    for (int i = 0; i < k; i++) {
        n *= s;
        s--;
    }
    return n;
}


void KKXIndex::pos_at_ix(EGPosition &pos, uint64_t ix, Color stm) const {
    uint64_t kkx = ix % N_KKX;
    ix = ix / N_KKX;
    pos.set_side_to_move(stm);

    bool allondiag = true;
    Square occupied_sqs[6];

    Square ktm_sq = Square(KKX_KTM_SQ[kkx]);
    allondiag = allondiag && (ktm_sq & DiagBB);
    Square kntm_sq = Square(KKX_KNTM_SQ[kkx]);
    allondiag = allondiag && (kntm_sq & DiagBB);
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
    int i = 0;
    for (PieceType p : sntm_pieces) {
        pieces[i] = make_piece(~stm, p);
        i++;
    }
    for (PieceType p : stm_pieces) {
        pieces[i] = make_piece(stm, p);
        i++;
    }


    uint64_t s = 62;
    i = 2;

    for (Piece p : pieces) {
        if (p == NO_PIECE) { break; }
        Square orig_sq = Square(ix % s);
        ix = ix / s;
        Square sq = orig_sq;

        // for (int j = 0; j < i; j++) {
        //     std::cout << j << ". " << int(occupied_sqs[j]) << std::endl;
        // }

        int k = 0;
        for (int j = 0; j < i; j++) {
            if (occupied_sqs[j] <= sq) {
                ++sq;
                k = j + 1;
            }
        }
        for (int j = i-1; j >= k; j--) {
            occupied_sqs[j+1] = occupied_sqs[j];
        }
        
        if (allondiag && !(sq & DiagBB)) {
            allondiag = false;
            if (sq & AboveDiagBB) {
                // put sq on bottom diag
                sq = Square(((sq >> 3) | (sq << 3)) & 63);
            }
        }

        i++;
        occupied_sqs[k] = sq;

        // std::cout << "initial p:" << PieceToChar[p] << ", ix:" << int(orig_sq) << std::endl;
        // std::cout << "insert final sq:" << int(sq) << " at " << k << std::endl;

        pos.put_piece(p, sq);
        s--;
    }
}

inline Square transform(const Square sq, int8_t flip, int8_t swap) {
    int8_t sq_ix = int8_t(sq) ^ flip;
    return Square(((sq_ix >> swap) | (sq_ix << swap)) & 63);
}



void print_transform(const EGPosition &pos) {
    Color stm = pos.side_to_move();

    Square orig_ktm_sq = pos.square<KING>(stm);
    Square orig_kntm_sq = pos.square<KING>(~stm);

    std::cout << "original" << std::endl;
    std::cout << pos << std::endl;

    KKX_IX_T kkx_ix_tr = get_kkx_ix_t(orig_ktm_sq, orig_kntm_sq);

    EGPosition pos2;
    std::memset(&pos2, 0, sizeof(EGPosition));
    for (PieceType pt = PAWN; pt <= KING; ++pt) {
        for (Color c: {WHITE, BLACK}) {
            Bitboard bb = pos.pieces(c, pt);
            while (bb) {
                Square sq = pop_lsb(bb);
                pos2.put_piece(make_piece(c,pt), transform(sq, kkx_ix_tr.flip, kkx_ix_tr.swap));
            }
        }
    }
    pos2.set_side_to_move(pos.side_to_move());
    std::cout << "transformed " << int(kkx_ix_tr.flip) << " " << int(kkx_ix_tr.swap) << std::endl;
    std::cout << pos2 << std::endl;
}

inline void maybe_update_swap(Square sq, int8_t flip, bool& allondiag, int8_t& swap) {
    // if this changes swap, we do not need to swap previous pieces since they are all on diagonal anyways
    if (allondiag) {
        if (!((sq ^ flip) & DiagBB)) {
            allondiag = false;
            swap = ((sq ^ flip) & AboveDiagBB) ? 3 : 0;
        }
    }
}

void transform_to(const EGPosition &pos, EGPosition &pos2) {
    Color stm = pos.side_to_move();
    bool allondiag = true;
    Square orig_ktm_sq = pos.square<KING>(stm);
    int8_t flip = ((orig_ktm_sq & RightHalfBB) ? 7 : 0) ^ ((orig_ktm_sq & TopHalfBB) ? 56 : 0);
    int8_t swap = 0;
    maybe_update_swap(orig_ktm_sq, flip, allondiag, swap);
    pos2.put_piece(make_piece(stm, KING), transform(orig_ktm_sq, flip, swap));

    Square orig_kntm_sq = pos.square<KING>(~stm);
    maybe_update_swap(orig_kntm_sq, flip, allondiag, swap);
    pos2.put_piece(make_piece(~stm, KING), transform(orig_kntm_sq, flip, swap));

    KKX_IX_T kkx_ix_tr = get_kkx_ix_t(orig_ktm_sq, orig_kntm_sq);
    assert (flip == kkx_ix_tr.flip);

    for (PieceType pt: {QUEEN, ROOK, BISHOP, KNIGHT}) {
        for (Color c: {~stm, stm}) {
            Bitboard bb = pos.pieces(c, pt);
            if (bb) {
                Square sq = lsb(bb);
                maybe_update_swap(sq, flip, allondiag, swap);
                pos2.put_piece(make_piece(c,pt), transform(sq, flip, swap));
            }
        }
    }
    pos2.set_side_to_move(pos.side_to_move());
}


uint64_t KKXIndex::ix_from_pos(EGPosition &pos) const {
    Color stm = pos.side_to_move();

    bool allondiag = true;

    Square orig_ktm_sq = pos.square<KING>(stm);

    int8_t flip = ((orig_ktm_sq & RightHalfBB) ? 7 : 0) ^ ((orig_ktm_sq & TopHalfBB) ? 56 : 0);
    int8_t swap = 0;
    maybe_update_swap(orig_ktm_sq, flip, allondiag, swap);
    Square ktm_sq = transform(orig_ktm_sq, flip, swap);


    Square orig_kntm_sq = pos.square<KING>(~stm);
    maybe_update_swap(orig_kntm_sq, flip, allondiag, swap);
    Square kntm_sq = transform(orig_kntm_sq, flip, swap);

    KKX_IX_T kkx_ix_tr = get_kkx_ix_t(orig_ktm_sq, orig_kntm_sq);
    assert (flip == kkx_ix_tr.flip);

    Square occupied_sqs[6];

    if (ktm_sq < kntm_sq) {
        occupied_sqs[0] = ktm_sq;
        occupied_sqs[1] = kntm_sq;
    } else {

        occupied_sqs[0] = kntm_sq;
        occupied_sqs[1] = ktm_sq;
    }
    
    
    uint64_t kkx = kkx_ix_tr.ix;

    uint64_t ix = kkx;
    uint64_t multiplier = N_KKX;
    // std::cout << "kkx " << kkx << std::endl;


    uint64_t s = 62;
    int i = 2;

    // TODO: replace with appropriate check
    if (popcount(pos.pieces(stm)) != int(stm_pieces.size())+1) {
        std::cout << "Wrong number of stm pieces" << std::endl;
        exit(1);
    }
    if (popcount(pos.pieces(~stm)) != int(sntm_pieces.size())+1) {
        std::cout << "Wrong number of sntm pieces" << std::endl;
        exit(1);
    }
    for (Color c: {~stm, stm}) {
        for (PieceType p: {QUEEN, ROOK, BISHOP, KNIGHT}) {
            Bitboard pieceBB = pos.pieces(c, p);
            if (pieceBB) {
                Square orig_sq = lsb(pieceBB);
                maybe_update_swap(orig_sq, flip, allondiag, swap);
                Square sq = transform(orig_sq, flip, swap);

                Square before_sq = sq;

                int k = i;
                for (int j = i-1; j >= 0; j--) {
                    if (occupied_sqs[j] < sq) {
                        --sq;
                    } else {
                        k = j;
                        occupied_sqs[j+1] = occupied_sqs[j];
                    }
                }
                
                // for (int j = 0; j < i; j++) {
                //     std::cout << j << ". " << int(occupied_sqs[j]) << std::endl;
                // }

                occupied_sqs[k] = before_sq;
                i++;

                // std::cout << "initial p:" << PieceToChar[p] << ", sq:" << int(orig_sq) << " insert at " << k << std::endl;
                // std::cout << "final ix:" << int(sq) << std::endl;

                ix += ((uint64_t) sq) * multiplier;
                multiplier *= s;

                s--;
            }
        }
    }
    return ix;
}

#endif