#include "prophet.h"
#include "bitboard.h"
#include "kkx.h"
#include "triangular_indexes.h"

#include <string>
#include "types.h"
#include "values.h"
#include "eg_position.h"
#include "eg_movegen.h"
#include "egtb.h"
#include "egtb_ids.h"
#include "compressed_tb.h"

#include <filesystem>
namespace fs = std::filesystem;

bool lazy_load = true;

// a side has at most 4 pieces besides the king, there are 126 such piece configurations
#define N_SIDE_CONFIGS 126
#define INVALID_SIDE_CONFIG 255
// indexed by piece counts packed with 3 bits per piece type
uint8_t SIDE_CONFIG_IX[1 << 15];
// indexed by stm config * N_SIDE_CONFIGS + sntm config
EGTB* EGTB_TABLE[N_SIDE_CONFIGS * N_SIDE_CONFIGS];
std::vector<EGTB*> egtbs = {};

inline int pack_piece_count(PieceType pt, int count) {
    return count << (3 * (pt - PAWN));
}

void init_side_config_ix() {
    int count = 0;
    for (int packed = 0; packed < (1 << 15); packed++) {
        int n_pieces = 0;
        bool valid = true;
        for (PieceType pt = PAWN; pt <= QUEEN; ++pt) {
            int n = (packed >> (3 * (pt - PAWN))) & 7;
            valid = valid && (n <= 4);
            n_pieces += n;
        }
        SIDE_CONFIG_IX[packed] = (valid && n_pieces <= 4) ? count++ : INVALID_SIDE_CONFIG;
    }
    assert (count == N_SIDE_CONFIGS);
}

inline int side_config_ix(const int pieces[6]) {
    int packed = 0;
    for (PieceType pt = PAWN; pt <= QUEEN; ++pt) {
        if (pieces[pt] > 4) return INVALID_SIDE_CONFIG;
        packed |= pack_piece_count(pt, pieces[pt]);
    }
    return SIDE_CONFIG_IX[packed];
}

inline int side_config_ix(const EGPosition &pos, Color c) {
    int packed = 0;
    for (PieceType pt = PAWN; pt <= QUEEN; ++pt) {
        if (pos.count(c, pt) > 4) return INVALID_SIDE_CONFIG;
        packed |= pack_piece_count(pt, pos.count(c, pt));
    }
    return SIDE_CONFIG_IX[packed];
}


void prophet_tb_init() {
    Bitboards::init();
    init_kkx_table();
    init_kkp_table();
    init_tril();
    init_egtb_id_to_ix();
    init_side_config_ix();

    for (int i = 0; i < N_SIDE_CONFIGS * N_SIDE_CONFIGS; i++) {
        EGTB_TABLE[i] = nullptr;
    }
    egtbs.clear();
}

int prophet_tb_add_path(const char* path) {
    int count = 0;
    for (const auto & entry : fs::recursive_directory_iterator(path)) {
        if (entry.path().extension() == COMP_EXT) {
            std::string folder = entry.path().parent_path().u8string();
            std::string filename = entry.path().filename().u8string();

            std::string egtb_id = filename.substr(0, filename.find_first_of(".")); 

            int stm_pieces[6];
            int sntm_pieces[6];
            egtb_id_to_pieces(egtb_id, stm_pieces, sntm_pieces);
            int stm_ix = side_config_ix(stm_pieces);
            int sntm_ix = side_config_ix(sntm_pieces);
            if (stm_ix == INVALID_SIDE_CONFIG || sntm_ix == INVALID_SIDE_CONFIG) continue;

            if (EGTB_TABLE[stm_ix * N_SIDE_CONFIGS + sntm_ix] == nullptr) {
                EGTB* egtb = new EGTB(folder, egtb_id);
                EGTB_TABLE[stm_ix * N_SIDE_CONFIGS + sntm_ix] = egtb;
                egtbs.push_back(egtb);
                count++;
            } 
        }
    }
    return count;
}

void prophet_tb_load_all_files() {
    lazy_load = false;
    for (EGTB* egtb : egtbs) {
        egtb->init_compressed_tb();
    }
}
size_t prophet_tb_get_size_on_disk_of_loaded_files() {
    size_t size = 0;
    for (EGTB* egtb : egtbs) {
        if (egtb->compressed) {
            size += egtb->CTB->compressed_filesize;
        }
    }
    return size;
}


void prophet_tb_deinit() {
    for (EGTB* egtb : egtbs) {
        delete egtb;
    }
    egtbs.clear();
    for (int i = 0; i < N_SIDE_CONFIGS * N_SIDE_CONFIGS; i++) {
        EGTB_TABLE[i] = nullptr;
    }
}

#define ERROR_TB_MISSING -1001
#define DRAW_VAL 1000

int dtm(int16_t raw) {
    if (raw == ERROR_TB_MISSING) {
        return ERROR_TB_MISSING;
    } else if (raw == 0) {
        return DRAW_VAL;
    } else if (raw > 0) {
        return WIN_IN(0) - raw;
    } else {
        return LOSS_IN(0) - raw;
    }
}


// pos is modified during search but restored on return
int16_t probe_position_raw_dctx(EGPosition &pos, DecompressCtx* dctx) {
    int stm_ix = side_config_ix(pos, pos.side_to_move());
    int sntm_ix = side_config_ix(pos, ~pos.side_to_move());
    if (stm_ix == INVALID_SIDE_CONFIG || sntm_ix == INVALID_SIDE_CONFIG) {
        return ERROR_TB_MISSING;
    }
    EGTB* egtb = EGTB_TABLE[stm_ix * N_SIDE_CONFIGS + sntm_ix];
    EGTB* mirror_egtb = EGTB_TABLE[sntm_ix * N_SIDE_CONFIGS + stm_ix];
    if (egtb == nullptr && mirror_egtb == nullptr) {
        return ERROR_TB_MISSING;
    }

    if (egtb != nullptr) {
        if (lazy_load) egtb->ensure_compressed_tb_initialised();
        return egtb->query_postion_dctx(pos, dctx);
    } else {
        EGMoveList movelist = EGMoveList(pos);
        if (movelist.size() == 0) {
            if (pos.stm_in_check()) {
                return LOSS_IN(0);
            } else {
                return 0;
            }
        }
        int16_t max_val = LOSS_IN(0);
        for (Move move : movelist) {
            UndoInfo u = pos.do_move(move);
            int16_t val = probe_position_raw_dctx(pos, dctx);
            max_val = std::max(max_val, (int16_t) -val);
            pos.undo_move(u);
        }
        if (max_val > 0) max_val--;
        if (max_val < 0) max_val++;
        return max_val;
    }
}
int probe_position_dtm_dctx(EGPosition &pos, DecompressCtx* dctx) {
    return dtm(probe_position_raw_dctx(pos, dctx));
}

int probe_position_dtm(EGPosition &pos) {
    DecompressCtx* dctx = new DecompressCtx(32768);
    int val = probe_position_dtm_dctx(pos, dctx);
    delete dctx;
    return val;
}

int16_t probe_position_raw_dtm(EGPosition &pos) {
    DecompressCtx* dctx = new DecompressCtx(32768);
    int val = probe_position_raw_dctx(pos, dctx);
    delete dctx;
    return val;
}


prophet_tb_decompress_ctx* prophet_tb_create_decompress_ctx() {
    return new DecompressCtx(32768);
}
void prophet_tb_free_decompress_ctx(prophet_tb_decompress_ctx* dctx) {
    delete dctx;
}


int prophet_tb_is_valid_position(const int pieces[6], const int squares[6], const int stm, const int ep_square) {
    if (stm < 0 || stm > 1) return -1;
    EGPosition pos;
    pos.reset();
    for (int i = 0; i < 6; i++) {
        if (pieces[i] < 0 || pieces[i] > 14) return -1;
        if (squares[i] < 0 || squares[i] > 64) return -1;
        if (pieces[i] != NO_PIECE) {
            if (squares[i] == SQ_NONE) return -1;
            if (pos.piece_on(Square(squares[i])) != NO_PIECE) return -3;
            pos.put_piece(Piece(pieces[i]), Square(squares[i]));
        }
    }
    if (pos.count(WHITE, KING) != 1 || pos.count(BLACK, KING) != 1) return -2;
    pos.set_side_to_move(Color(stm));

    if (ep_square < 0 || ep_square > 64) return -1;
    if (ep_square != 0 && ep_square != SQ_NONE) {
        if (!pos.check_ep(Square(ep_square))) return -5;
        pos.set_ep_square(Square(ep_square));
    }

    if (pos.sntm_in_check()) return -4;

    return 1;
}


int prophet_tb_probe_dtm_dctx(const int pieces[6], const int squares[6], const int stm, const int ep_square, prophet_tb_decompress_ctx* dctx) {
    EGPosition pos;
    pos.reset();
    for (int i = 0; i < 6; i++) {
        if (pieces[i] != NO_PIECE) {
            pos.put_piece(Piece(pieces[i]), Square(squares[i]));
        }
    }
    pos.set_side_to_move(Color(stm));
    if (ep_square != 0 && ep_square != SQ_NONE) pos.set_ep_square(Square(ep_square));
    return probe_position_dtm_dctx(pos, dctx);
}


int prophet_tb_probe_dtm(const int pieces[6], const int squares[6], const int stm, const int ep_square) {
    DecompressCtx* dctx = new DecompressCtx(32768);
    int val = prophet_tb_probe_dtm_dctx(pieces, squares, stm, ep_square, dctx);
    delete dctx;
    return val;
}