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
std::unordered_map<std::string, EGTB*> id_to_egtb = {};


void prophet_tb_init() {
    Bitboards::init();
    init_kkx_table();
    init_kkp_table();
    init_tril();
    init_egtb_id_to_ix();

    for (std::string egtb_id : get_egtb_identifiers()) {
        id_to_egtb[egtb_id] = nullptr;
    }
}

int prophet_tb_add_path(const char* path) {
    int count = 0;
    for (const auto & entry : fs::recursive_directory_iterator(path)) {
        if (entry.path().extension() == COMP_EXT) {
            std::string folder = entry.path().parent_path().u8string();
            std::string filename = entry.path().filename().u8string();

            std::string egtb_id = filename.substr(0, filename.find_first_of(".")); 

            if (id_to_egtb[egtb_id] == nullptr) {
                EGTB* egtb = new EGTB(folder, egtb_id);
                id_to_egtb[egtb_id] = egtb;
                count++;
            } 
        }
    }
    return count;
}

void prophet_tb_load_all_files() {
    lazy_load = false;
    for (auto & entry : id_to_egtb) {
        if (entry.second != nullptr) entry.second->init_compressed_tb();
    }
}
size_t prophet_tb_get_size_on_disk_of_loaded_files() {
    size_t size = 0;
    for (auto & entry : id_to_egtb) {
        if (entry.second != nullptr && entry.second->compressed) {
            size += entry.second->CTB->compressed_filesize;
        }
    }
    return size;
}


void prophet_tb_deinit() {
    for (auto & entry : id_to_egtb) {
        if (entry.second != nullptr) {
            delete entry.second;
        }
    }
    for (std::string egtb_id : get_egtb_identifiers()) {
        id_to_egtb[egtb_id] = nullptr;
    }
}


std::string egtb_id_from_pos(const EGPosition pos) {
    std::ostringstream os;
    for (Color c : {pos.side_to_move(), ~pos.side_to_move()}) {
        for (PieceType pt : {KING, QUEEN, ROOK, BISHOP, KNIGHT, PAWN}) {
            for (int i = 0; i < pos.count(c, pt); i++) {
                os << PieceToChar[pt];
            }
        }
    }
    return os.str();
}

#define ERROR_TB_MISSING -1001

int dtm(int16_t raw) {
    if (raw == ERROR_TB_MISSING) {
        return ERROR_TB_MISSING;
    } else if (raw == 0) {
        return 0;
    } else if (raw > 0) {
        return WIN_IN(0) - raw;
    } else {
        return raw - LOSS_IN(0);
    }
}


int16_t probe_position_raw_dctx(EGPosition pos, DecompressCtx* dctx) {
    std::string egtb_id = egtb_id_from_pos(pos);
    std::string mirror_id = get_mirror_id(egtb_id);
    if (id_to_egtb[egtb_id] == nullptr && id_to_egtb[mirror_id] == nullptr) {
        return ERROR_TB_MISSING;
    }

    if (id_to_egtb[egtb_id] != nullptr) {
        if (lazy_load) id_to_egtb[egtb_id]->ensure_compressed_tb_initialised();
        return id_to_egtb[egtb_id]->query_postion_dctx(pos, dctx);
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
int probe_position_dtm_dctx(EGPosition pos, DecompressCtx* dctx) {
    return dtm(probe_position_raw_dctx(pos, dctx));
}

int probe_position_dtm(EGPosition pos) {
    DecompressCtx* dctx = new DecompressCtx(32768);
    int val = probe_position_dtm_dctx(pos, dctx);
    delete dctx;
    return val;
}

int16_t probe_position_raw_dtm(EGPosition pos) {
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
        if (squares[i] < 0 || squares[i] > 63) return -1;
        if (pieces[i] != NO_PIECE) {
            if (pos.piece_on(Square(squares[i])) != NO_PIECE) return -3;
            pos.put_piece(Piece(pieces[i]), Square(squares[i]));
        }
    }
    if (pos.count(WHITE, KING) != 1 || pos.count(BLACK, KING) != 1) return -2;
    pos.set_side_to_move(Color(stm));

    if (ep_square < 0 || ep_square > 63) return -1;
    if (ep_square != 0) {
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
    if (ep_square != 0) pos.set_ep_square(Square(ep_square));
    return probe_position_dtm_dctx(pos, dctx);
}


int prophet_tb_probe_dtm(const int pieces[6], const int squares[6], const int stm, const int ep_square) {
    DecompressCtx* dctx = new DecompressCtx(32768);
    int val = prophet_tb_probe_dtm_dctx(pieces, squares, stm, ep_square, dctx);
    delete dctx;
    return val;
}