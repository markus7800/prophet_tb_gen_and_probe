#include <iostream>
#include <vector>
#include "bitboard.h"
#include "kkx.h"
// #include "kkp.h"
#include "linearize.h"
#include "eg_position.h"
#include "eg_movegen.h"
#include "gen_egtb.h"
#include "triangular_indexes.h"
#include <unordered_set>
#include <omp.h>

#include <unistd.h> // close
#include <fcntl.h> // open
#include <sys/mman.h> // mmap
#include <sys/stat.h> // file size


void test_index() {

    // 0, PAWN, KNIGHT, BISHOP, ROOK, QUEEN 
    int wpieces[6] = {0, 1, 0, 0, 0, 0};
    int bpieces[6] = {0, 1, 0, 0, 0, 0};

    uint64_t count = 0;
    for (Color stm: {WHITE, BLACK}) {

        EGTB egtb = stm == WHITE ? EGTB(wpieces, bpieces) : EGTB(bpieces, wpieces);
        std::cout << egtb.id << ": nonep: " << egtb.num_nonep_pos << ", ep: " << egtb.num_ep_pos << std::endl;


        PieceType pts[4] = {NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE};
        Color cs[4] = {stm, stm, stm, stm};

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
        EGPosition pos4;


        for (Square k1_sq = SQ_A1; k1_sq <= SQ_H8; ++k1_sq) {
            for (Square k2_sq = SQ_A1; k2_sq <= SQ_H8; ++k2_sq) {
                if (k2_sq == k1_sq) { continue; }
                for (Square p1_sq = SQ_A1; p1_sq <= SQ_H8; ++p1_sq) {
                    if (p1_sq == k1_sq || p1_sq == k2_sq) { continue; }
                    for (Square p2_sq = SQ_A1; p2_sq <= SQ_H8; ++p2_sq) {
                        if (p2_sq == k1_sq || p2_sq == k2_sq || p2_sq == p1_sq) { continue; }
                        // for (Square p3_sq = SQ_A1; p3_sq <= SQ_H8; ++p3_sq) {
                        //     if (p3_sq == k1_sq || p3_sq == k2_sq || p3_sq == p1_sq || p3_sq == p2_sq) { continue; }
                        //     for (Square p4_sq = SQ_A1; p4_sq <= SQ_H8; ++p4_sq) {
                        //         if (p4_sq == k1_sq || p4_sq == k2_sq || p4_sq == p1_sq || p4_sq == p2_sq || p4_sq == p3_sq) { continue; }

                                if ((PseudoAttacks[KING][k1_sq] & k2_sq) == 0) {
                                    pos1.reset();
                                    pos2.reset();
                                    pos3.reset();
                                    pos4.reset();
                                    pos1.put_piece(make_piece(stm, KING), k1_sq);
                                    pos1.put_piece(make_piece(~stm,KING), k2_sq);
                                    if (pts[0] == PAWN && !(p1_sq & PawnSquaresBB)) { continue; }
                                    pos1.put_piece(make_piece(cs[0],pts[0]), p1_sq);
                                    if (pts[1] == PAWN && !(p2_sq & PawnSquaresBB)) { continue; }
                                    pos1.put_piece(make_piece(cs[1],pts[1]), p2_sq);
                                    // if (pts[2] == PAWN && !(p3_sq & PawnSquaresBB)) { continue; }
                                    // pos1.put_piece(make_piece(cs[2],pts[2]), p3_sq);
                                    // if (pts[3] == PAWN && !(p4_sq & PawnSquaresBB)) { continue; }
                                    // pos1.put_piece(make_piece(cs[3],pts[3]), p4_sq);
                                    pos1.set_side_to_move(stm);

                                    uint64_t ix = ix_from_pos(pos1, egtb.num_nonep_pos, egtb.num_ep_pos);

                                    // bool verbose = true;
                                    bool verbose = false;
                                    // bool verbose = (ix == 1772);
                                    // bool verbose = (p1_sq == SQ_B1 && p2_sq == SQ_A7 && k2_sq == SQ_A1 && k1_sq == SQ_C2);

                                    // bool debug = true;
                                    bool debug = false;
                                    // bool debug = verbose;

                                    if (verbose) std::cout << pos1;

                                    if (verbose) std::cout << "A ix from pos\n";
                                    if (verbose) std::cout << "B pos at ix " << ix << "\n";
                                    pos_at_ix(pos2, ix, stm, wpieces, bpieces, egtb.num_nonep_pos, egtb.num_ep_pos);
                                    if (verbose) std::cout << "C transform to canonical\n";
                                    transform_to_canoncial(pos1, pos3);
                                    if (verbose) std::cout << "D ix from canonical\n";
                                    uint64_t ix2 = ix_from_pos(pos3, egtb.num_nonep_pos, egtb.num_ep_pos);
                                    if (verbose) std::cout << "E transformed pos at ix " << ix2 << "\n";
                                    pos_at_ix(pos4, ix2, stm, wpieces, bpieces, egtb.num_nonep_pos, egtb.num_ep_pos);
                                    if (verbose) std::cout << "F\n";

                                    if (!pos2.is_equal(pos3) || ix != ix2 || !pos3.is_equal(pos4) || debug) {
                                        std::cout << pos1 << std::endl;
                                        std::cout << "vs at ix " << ix << std::endl;
                                        std::cout << pos2 << std::endl;
                                        std::cout << "vs transformed " << std::endl;
                                        std::cout << pos3 << std::endl;
                                        std::cout << "vs at ix " << ix2 << std::endl;
                                        std::cout << pos4 << std::endl;
                                        exit(1);
                                    }
                                    count++;
                                    // std::cout << "\n";
                                }
                        //     }
                        // }
                    }
                }
            }
        }
    }
    std::cout << "Checked " << count << " kkx positions" << std::endl;
    
}


void test_ep_index() {

    // 0, PAWN, KNIGHT, BISHOP, ROOK, QUEEN 
    int wpieces[6] = {0, 1, 0, 0, 0, 0};
    int bpieces[6] = {0, 1, 0, 0, 0, 0};

    uint64_t count = 0;

    for (Color stm: {WHITE, BLACK}) {
        EGTB egtb = stm == WHITE ? EGTB(wpieces, bpieces) : EGTB(bpieces, wpieces);
        std::cout << egtb.id << ": nonep: " << egtb.num_nonep_pos << ", ep: " << egtb.num_ep_pos << std::endl;

        EGPosition pos1;
        EGPosition pos2;
        EGPosition pos3;
        EGPosition pos4;

        for (Square p1_sq = SQ_A1; p1_sq <= SQ_H8; ++p1_sq) {
            if (!(square_bb(p1_sq) & (Rank4BB | Rank5BB))) continue;
            for (Square p2_sq = SQ_A1; p2_sq <= SQ_H8; ++p2_sq) {
                if (!(square_bb(p2_sq) & (Rank4BB | Rank5BB))) continue;
                if (p2_sq == p1_sq) continue;
                for (Square ep_sq = SQ_A1; ep_sq <= SQ_H8; ++ep_sq) {
                    if (!(square_bb(ep_sq) & (Rank3BB | Rank6BB))) continue;

                    pos1.reset();
                    pos2.reset();
                    pos3.reset();
                    pos4.reset();
                    pos1.put_piece(make_piece(WHITE, KING), SQ_A1);
                    pos1.put_piece(make_piece(BLACK,KING), SQ_A8);
                    pos1.put_piece(make_piece(stm,PAWN), p1_sq);
                    pos1.put_piece(make_piece(~stm,PAWN), p2_sq);
                    pos1.set_side_to_move(stm);

                    if (!pos1.check_ep(ep_sq)) { continue;}
                    pos1.set_ep_square(ep_sq);


                    bool verbose = true;
                    bool debug = false;

                    // if (verbose) std::cout << pos1;
                    std::cout << pos1;


                    if (verbose) std::cout << "A ix from pos\n";
                    uint64_t ix = ix_from_pos(pos1, egtb.num_nonep_pos, egtb.num_ep_pos);
                    if (verbose) std::cout << "B pos at ix " << ix << "\n";
                    pos_at_ix(pos2, ix, stm, wpieces, bpieces, egtb.num_nonep_pos, egtb.num_ep_pos);
                    if (verbose) std::cout << "C transform to canonical\n";
                    transform_to_canoncial(pos1, pos3);
                    if (verbose) std::cout << "D ix from canonical\n";
                    uint64_t ix2 = ix_from_pos(pos3, egtb.num_nonep_pos, egtb.num_ep_pos);
                    if (verbose) std::cout << "E transformed pos at ix " << ix2 << "\n";
                    pos_at_ix(pos4, ix2, stm, wpieces, bpieces, egtb.num_nonep_pos, egtb.num_ep_pos);
                    if (verbose) std::cout << "F\n";

                    if (!pos2.is_equal(pos3) || ix != ix2 || !pos3.is_equal(pos4) || debug) {
                        std::cout << pos1 << std::endl;
                        std::cout << "vs at ix " << ix << std::endl;
                        std::cout << pos2 << std::endl;
                        std::cout << "vs transformed " << std::endl;
                        std::cout << pos3 << std::endl;
                        std::cout << "vs at ix " << ix2 << std::endl;
                        std::cout << pos4 << std::endl;
                        exit(1);
                    }

                    count++;
                }
            }
        }
    }    
    std::cout << "Checked " << count << " ep positions" << std::endl;
}

void place_piece(Piece p, int* pieces1, int* pieces2) {
    int* pieces = color_of(p) == WHITE ? pieces1 : pieces2;
    pieces[type_of(p)]++;
}
void unplace_piece(Piece p, int* pieces1, int* pieces2) {
    int*pieces = color_of(p) == WHITE ? pieces1 : pieces2;
    pieces[type_of(p)]--;
}

int main(int argc, char *argv[]) {
    // single threaded: 3m15s
    Bitboards::init();
    init_kkx_table();
    init_tril();
    // test_index();
    // test_ep_index();
    // exit(0);


    EGPosition pos;
    /*
    pos.reset();
    pos.put_piece(W_KING, SQ_A1);
    pos.put_piece(B_KING, SQ_H1);
    pos.put_piece(W_PAWN, SQ_A2);
    pos.put_piece(W_PAWN, SQ_E2);
    pos.put_piece(W_PAWN, SQ_H2);
    pos.put_piece(B_PAWN, SQ_B4);
    pos.put_piece(B_PAWN, SQ_D4);
    pos.put_piece(B_PAWN, SQ_F4);

    // pos.do_move(Move(SQ_A2, SQ_A4)); // one pe
    pos.do_move(Move(SQ_E2, SQ_E4)); // two ep
    // pos.do_move(Move(SQ_H2, SQ_H4)); // no ep

    std::cout << pos;
    std::cout << "Forward moves:\n";
    for (Move m: EGMoveList<FORWARD>(pos)) {
        std::cout << move_to_uci(m) << " " << int(m.type_of()>>14) << std::endl;
    }*/


    /*
    pos.reset();
    pos.put_piece(W_KING, SQ_A1);
    pos.put_piece(B_KING, SQ_H1);
    pos.put_piece(W_PAWN, SQ_A4);
    pos.put_piece(W_PAWN, SQ_E4);
    pos.put_piece(W_PAWN, SQ_H4);
    pos.put_piece(B_PAWN, SQ_B4);
    pos.put_piece(B_PAWN, SQ_D4);
    pos.put_piece(B_PAWN, SQ_F4);
    pos.set_side_to_move(BLACK);

    std::cout << pos;
    std::cout << "Reverse moves:\n";
    for (Move m: EGMoveList<REVERSE>(pos)) {
        std::cout << move_to_uci(m) << " " << int(m.type_of()>>14) << std::endl;
    }*/

    //exit(0);

    assert (argc > 0);
    int nthreads = atoi(argv[1]);

    std::vector<int> pieces1(6);
    std::vector<int> pieces2(6);

    // pieces1 = {0, 1, 1, 0, 0, 0};
    // pieces2 = {0, 1, 0, 0, 0, 0};
    // EGTB _egtb = EGTB(&pieces1[0], &pieces2[0]);
    // pos.reset();
    // pos_at_ix(pos, 257392654, BLACK, &pieces1[0], &pieces2[0], _egtb.num_nonep_pos, _egtb.num_ep_pos);
    // std::cout << pos << std::endl;
    // uint64_t ix = ix_from_pos(pos, _egtb.num_nonep_pos, _egtb.num_ep_pos);
    // std::cout << ix << std::endl;
    // for (Move move : EGMoveList<REVERSE>(pos)) {
    //     std::cout << move_to_uci(move) << std::endl;
    // }
    // exit(0);

    
    /*
    pieces1 = {0, 2, 0, 0, 0, 0};
    pieces2 = {0, 0, 0, 0, 0, 0};

    uint64_t LOSS_NPOS = compute_num_positions(&pieces1[0], &pieces2[0]);
    uint64_t N_UNUSED = 0;
    uint64_t N_SNTM_IN_CHECK = 0;

    for (uint64_t ix = 0; ix < LOSS_NPOS; ix++) {
        pos.reset();
        pos_at_ix(pos, ix, WHITE, &pieces1[0], &pieces2[0]);
        if (ix_from_pos(pos) != ix && !pos.sntm_in_check()) {
            N_UNUSED++;
            // EGPosition pos2;
            // pos2.reset();
            // pos_at_ix(pos2, ix_from_pos(pos), BLACK, &pieces1[0], &pieces2[0]);
            // std::cout << pos << ix << " unused" << std::endl;
            // std::cout << pos2 << ix_from_pos(pos) << "\n\n";
        }
        if (pos.sntm_in_check()) {
            // std::cout << pos << ix << " sntm_in_check" << std::endl;
            N_UNUSED++;
            N_SNTM_IN_CHECK++;
        }
        // if (N_UNUSED > 100) { break; }
        // if (N_SNTM_IN_CHECK > 100) { break; }
    }
    std::cout << N_UNUSED << std::endl;
    std::cout << N_SNTM_IN_CHECK << std::endl;

    exit(0);
    */
    
    pieces1 = {0, 0, 0, 0, 0, 0};
    pieces2 = {0, 0, 0, 0, 0, 0};

    Piece PIECES_ARR[] = {NO_PIECE, W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN};

    GenEGTB* g;
    // EGPosition pos;
    int16_t longest_overall_mate = WIN_IN(0) + 1;
    std::string longest_overall_mate_str;
    bool check_longest_mate = true;

    std::unordered_set<std::string> egtbs = {};
    
    uint64_t val_count[512] = {0};
    uint64_t total_poscount = 0;

    for (int piece_count = 0; piece_count <= 3; piece_count++) {
        for (int pawn_count = 0; pawn_count <= piece_count; pawn_count++ ) {
            for (Piece p1 : PIECES_ARR) {
                for (Piece p2 : PIECES_ARR) {
                    for (Piece p3 : PIECES_ARR) {
                        if ((p1 != NO_PIECE) + (p2 != NO_PIECE) + (p3 != NO_PIECE) != piece_count) continue;
                        if ((piece_count == 0) && (p1 != NO_PIECE) + (p2 != NO_PIECE) + (p3 != NO_PIECE) > 0) continue;
                        if ((piece_count == 1) && (p2 != NO_PIECE) + (p3 != NO_PIECE) > 0) continue;
                        if ((piece_count == 2) && (p3 != NO_PIECE) > 0) continue;

                        if ((type_of(p1) == PAWN) + (type_of(p2) == PAWN) + (type_of(p3) == PAWN) != pawn_count) {
                            continue;
                        }

                        if (p1 != NO_PIECE) place_piece(p1, &pieces1[0], &pieces2[0]);
                        if (p2 != NO_PIECE) place_piece(p2, &pieces1[0], &pieces2[0]);
                        if (p3 != NO_PIECE) place_piece(p3, &pieces1[0], &pieces2[0]);

                        std::string id = get_egtb_identifier(&pieces1[0], &pieces2[0]);
                        auto p = egtbs.insert(id);

                        if (p.second) { // true if inserted
                            g = new GenEGTB(&pieces1[0], &pieces2[0], "egtbs/");
                            g->gen(nthreads);
                            g->~GenEGTB();

                            if (check_longest_mate) {
                                std::cout << id << ": ";
                                
                                EGTB egtb = EGTB(&pieces1[0], &pieces2[0]);
                                load_egtb(&egtb, "egtbs/", true);

                                int16_t longest_mate = WIN_IN(0) + 1;
                                uint64_t longest_mate_ix = 0;
                                uint64_t unused_count = 0;
                                total_poscount += egtb.num_pos;
                                for (uint64_t win_ix = 0; win_ix < egtb.num_pos; win_ix++) {
                                    int16_t val = egtb.TB[win_ix];
                                    assert (IS_SET(val));
                                    if (val == 0) {
                                        val_count[0]++;
                                    } else {
                                        val_count[WIN_IN(0) - abs(val) + 1]++;
                                    }
                                    if (0 < val && val < longest_mate) {
                                        longest_mate = val;
                                        longest_mate_ix = win_ix;
                                    }
                                }

                                if (longest_mate == WIN_IN(0) + 1) {
                                    std::cout << "no win." << std::endl;
                                } else {
                                    pos.reset();
                                    pos_at_ix(pos, longest_mate_ix, WHITE, &pieces1[0], &pieces2[0], egtb.num_nonep_pos, egtb.num_nonep_pos);
                                    std::cout << pos.fen() << " " << WIN_IN(0) - egtb.TB[longest_mate_ix];
                                    if (longest_mate < longest_overall_mate) {
                                        longest_overall_mate = longest_mate;
                                        std::cout << "*";

                                        std::ostringstream oss;
                                        oss << get_egtb_identifier(&pieces1[0], &pieces2[0]) << ": " << pos.fen() << " " << WIN_IN(0) - egtb.TB[longest_mate_ix];
                                        longest_overall_mate_str = oss.str();
                                    }
                                    std::cout << std::endl;
                                }
                                std::cout << "#unused " << unused_count << " (" << (double) unused_count / egtb.num_pos * 100 << "%)" << std::endl;

                                if (!egtb_exists(&egtb, "egtbs8/")) {
                                    store_egtb_8bit(&egtb, "egtbs8/");
                                }
                                free_egtb(&egtb);
                            }
                        }

                        if (p1 != NO_PIECE) unplace_piece(p1, &pieces1[0], &pieces2[0]);
                        if (p2 != NO_PIECE) unplace_piece(p2, &pieces1[0], &pieces2[0]);
                        if (p3 != NO_PIECE) unplace_piece(p3, &pieces1[0], &pieces2[0]);

                    }
                }
            }
        }
    }

    std::cout << "Longest mate: " << longest_overall_mate_str << std::endl;

    // for (int i = 0; i < 512; i++) {
    //     printf("%3d: %10lu,\n", i, val_count[i]);
    // }
    std::cout << "total count: " << total_poscount << std::endl;

    return 0;
}