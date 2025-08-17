
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

    void pos_at_ix(EGPosition &pos, uint64_t ix) const;
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


void KKXIndex::pos_at_ix(EGPosition &pos, uint64_t ix) const {
    uint64_t kkx = ix % N_KKX;
    ix = ix / N_KKX;
    pos.set_side_to_move(BLACK);


    Square occupied_sqs[6];

    Square ktm_sq = Square(KKX_KTM_SQ[kkx]);
    Square kntm_sq = Square(KKX_KNTM_SQ[kkx]);
    if (ktm_sq < kntm_sq) {
        occupied_sqs[0] = ktm_sq;
        occupied_sqs[1] = kntm_sq;
    } else {

        occupied_sqs[0] = kntm_sq;
        occupied_sqs[1] = ktm_sq;
    }
    pos.put_piece(B_KING, ktm_sq);
    pos.put_piece(W_KING, kntm_sq);


    Piece pieces[4] = {NO_PIECE,NO_PIECE,NO_PIECE,NO_PIECE};
    int i = 0;
    for (PieceType p : sntm_pieces) {
        pieces[i] = make_piece(WHITE, p);
        i++;
    }
    for (PieceType p : stm_pieces) {
        pieces[i] = make_piece(BLACK, p);
        i++;
    }


    uint64_t s = 62;
    i = 2;

    for (Piece p : pieces) {
        if (p == NO_PIECE) { break; }
        Square orig_sq = Square(ix % s);
        Square sq = orig_sq;
        ix = ix / s;

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
        
        i++;
        occupied_sqs[k] = sq;

        // std::cout << "initial p:" << PieceToChar[p] << ", ix:" << int(orig_sq) << std::endl;
        // std::cout << "insert final sq:" << int(sq) << " at " << k << std::endl;

        pos.put_piece(p, sq);
        s--;
    }
}
uint64_t KKXIndex::ix_from_pos(EGPosition &pos) const {
    Color stm = pos.side_to_move();

    Square occupied_sqs[6];

    Square ktm_sq = pos.square<KING>(stm);
    Square kntm_sq = pos.square<KING>(~stm);


    if (ktm_sq < kntm_sq) {
        occupied_sqs[0] = ktm_sq;
        occupied_sqs[1] = kntm_sq;
    } else {

        occupied_sqs[0] = kntm_sq;
        occupied_sqs[1] = ktm_sq;
    }
    
    KKX_IX_T kkx_ix_t = KKX_IX_T_TABLE[ktm_sq][kntm_sq];
    
    uint64_t kkx = kkx_ix_t.ix;

    uint64_t ix = kkx;
    uint64_t multiplier = N_KKX;
    // std::cout << "kkx " << kkx << std::endl;


    uint64_t s = 62;
    int i = 2;

    for (Color c: {~stm, stm}) {
        for (PieceType p: {QUEEN, ROOK, BISHOP, KNIGHT}) {
            Bitboard pieceBB = pos.pieces(~c, p);
            if (pieceBB) {
                // TODO: transform square
                Square orig_sq = lsb(pieceBB);
                Square sq = orig_sq;

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

                occupied_sqs[k] = orig_sq;
                i++;

                // std::cout << "initial p:" << PieceToChar[p] << ", sq:" << int(orig_sq) << " insert at " << k << std::endl;
                // std::cout << "final ix:" << int(sq) << std::endl;

                ix += (sq * multiplier);
                multiplier *= s;

                s--;
            }
        }
    }
    return ix;
}

#endif