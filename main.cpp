#include <iostream>
#include <vector>
#include "bitboard.h"
#include "kkx.h"
// #include "kkp.h"
#include "linearize.h"
#include "eg_position.h"
#include "eg_movegen.h"
#include "gen_egtb.h"

void test_index() {
    Color stm = BLACK;

    // 0, PAWN, KNIGHT, BISHOP, ROOK, QUEEN 
    int stm_pieces[6]  = {0, 1, 0, 0, 0, 0};
    int sntm_pieces[6] = {0, 0, 0, 0, 0, 1};


    PieceType pts[4] = {NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE};
    Color cs[4] = {stm, stm, stm, stm};

    int* wpieces = (int*) ((stm == WHITE) ? &stm_pieces : &sntm_pieces);
    int* bpieces = (int*) ((stm == WHITE) ? &sntm_pieces : &stm_pieces);

    int i = 0;
    for (Color c: {~stm, stm}) {
        int* c_pieces = (c == WHITE) ? wpieces : bpieces;
        for (PieceType pt = QUEEN; pt >= PAWN; --pt) {
            for (int j = 0; j < c_pieces[pt]; ++j) {
                if (i >= 4) { std::cout << "Too many pieces!\n"; assert(false); }
                pts[i] = pt;
                cs[i] = c;
                i++;
            }
        }
    }

    EGPosition pos1;
    EGPosition pos2;
    EGPosition pos3;

    uint64_t count = 0;

    for (Square k1_sq = SQ_A1; k1_sq <= SQ_H8; ++k1_sq) {
        for (Square k2_sq = SQ_A1; k2_sq <= SQ_H8; ++k2_sq) {
            if (k2_sq == k1_sq) { continue; }
            for (Square p1_sq = SQ_A1; p1_sq <= SQ_H8; ++p1_sq) {
                if (p1_sq == k1_sq || p1_sq == k2_sq) { continue; }
                for (Square p2_sq = SQ_A1; p2_sq <= SQ_H8; ++p2_sq) {
                    if (p2_sq == k1_sq || p2_sq == k2_sq || p2_sq == p1_sq) { continue; }

                    if ((PseudoAttacks[KING][k1_sq] & k2_sq) == 0) {
                        pos1.reset();
                        pos2.reset();
                        pos3.reset();
                        pos1.put_piece(B_KING, k1_sq);
                        pos1.put_piece(W_KING, k2_sq);
                        if (pts[0] == PAWN && !(p1_sq & PawnSquaresBB)) { continue; }
                        if (pts[1] == PAWN && !(p2_sq & PawnSquaresBB)) { continue; }
                        pos1.put_piece(make_piece(cs[0],pts[0]), p1_sq);
                        pos1.put_piece(make_piece(cs[1],pts[1]), p2_sq);
                        pos1.set_side_to_move(stm);

                        // std::cout << "A\n";
                        uint64_t ix = ix_from_pos(pos1);
                        // std::cout << "B\n";
                        pos_at_ix(pos2, ix, stm, wpieces, bpieces);
                        // std::cout << "C\n";
                        transform_to(pos1, pos3);
                        // std::cout << "D\n";
                        uint64_t ix2 = ix_from_pos(pos3);
                        // std::cout << "E\n";

                        if (!pos2.is_equal(pos3) || ix != ix2) {
                            std::cout << pos1 << std::endl;
                            std::cout << "vs at ix " << ix << std::endl;
                            std::cout << pos2 << std::endl;
                            std::cout << "vs transformed at ix " << ix2 << std::endl;
                            std::cout << pos3 << std::endl;
                            exit(1);
                        }
                        count++;
                    }
                }
            }
        }
    }
    std::cout << "Checked " << count << " kkx positions" << std::endl;
    
}


int main() {
    Bitboards::init();
    init_kkx_table();

    test_index();

    // exit(0);

    std::vector<int> pieces1(6);
    std::vector<int> pieces2(6);
    
    pieces1 = {0, 0, 0, 0, 0, 0};
    pieces2 = {0, 0, 0, 0, 0, 0};
    GenEGTB g = GenEGTB(&pieces1[0], &pieces2[0]);
    g.gen();
    
    // 3 men, no pawns
    /*
    for (PieceType pt = KNIGHT; pt <= QUEEN; ++pt) {
        pieces1[pt]++;
        g = GenEGTB(&pieces1[0], &pieces2[0]);
        g.gen();
        pieces1[pt]--;
    }
    */


    // 4 men
    pieces1 = {0, 1, 0, 0, 0, 0};
    pieces2 = {0, 0, 0, 0, 0, 0};

    pieces1 = {0, 0, 0, 0, 0, 1};
    pieces2 = {0, 0, 0, 0, 1, 0};

    g = GenEGTB(&pieces1[0], &pieces2[0]);
    g.gen();

    return 0;
}