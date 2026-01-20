#ifndef PROPHET_H
#define PROPHET_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// initialisation is not thread-safe

void prophet_tb_init();
void prophet_tb_deinit();

// recursively searches path and adds TB files
// returns number of files added
int prophet_tb_add_path(const char* path);

// if this function is not called, files will be lazily loaded on probe
void prophet_tb_load_all_files();
size_t prophet_tb_get_size_on_disk_of_loaded_files();

// pieces = 0, ..., 14
//  0 = NO_PIECE
//  1 = W_PAWN
//  2 = W_KNIGHT
//  3 = W_BISHOP
//  4 = W_ROOK
//  5 = W_QUEEN
//  6 = W_KING
//  9 = B_PAWN
// 10 = B_KNIGHT
// 11 = B_BISHOP
// 12 = B_ROOK
// 13 = B_QUEEN
// 14 = B_KING

// squares = 0, ..., 63
//  0 = SQ_A1,  1 = SQ_B1, ...,  7 = SQ_H1
//  8 = SQ_A2,  9 = SQ_B2, ..., 15 = SQ_H2
// ...
// 56 = SQ_A8, 57 = SQ_B8, ..., 63 = SQ_H8

// stm = 0 or 1 stm
// 0 = WHITE
// 1 = BLACK

// returns v=0 if draw or illegal position
// returns 1000>v>0 if win in v plies
// returns -1000<v<0 if loss in v plies
// returns -1001 if table is completely missing (both sides, if one side is missing it performs search)
// is thread-safe
int prophet_tb_probe_dtm(const int pieces[6], const int squares[6], const int stm, const int ep_square);

// for better performance reuse DecompressCtx
// for multi-threading use one DecompressCtx per thread
// all functions below are thread-safe
typedef struct DecompressCtx DecompressCtx;
DecompressCtx* CreateDecompressCtx();
void FreeDecompressCtx(DecompressCtx* dctx);
int prophet_tb_probe_dtm_dctx(const int pieces[6], const int squares[6], const int stm, const int ep_square, DecompressCtx* dctx);


#ifdef __cplusplus
}
#endif

#endif