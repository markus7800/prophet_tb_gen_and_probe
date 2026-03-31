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


// arguments description

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

// squares = 0, ..., 64
//  0 = SQ_A1,  1 = SQ_B1, ...,  7 = SQ_H1
//  8 = SQ_A2,  9 = SQ_B2, ..., 15 = SQ_H2
// ...
// 56 = SQ_A8, 57 = SQ_B8, ..., 63 = SQ_H8
// 64 = SQ_NONE

// stm = 0 or 1 stm
// 0 = WHITE
// 1 = BLACK

// use ep_square = 0 or SQ_NONE = 64 to indicate no en-passant possible in position
// otherwise, en-passant has to be a legal move with ep_square as destination square for capturing pawn

// use pieces[i] = NO_PIECE to indicate absence of i-th piece.

// if positions is valid, prophet_tb_is_valid_position returns 1
// this includes following checks:
// - are pieces valid, 0 <= p <= 14, otherwise returns -1
// - are squares valid, 0 <= s <= 64, otherwise returns -1
// - are squares[i] != SQ_NONE for pieces[i] != NO_PIECE, otherwise returns -1
// - is stm valid, stm = 0 or 1, otherwise returns -1
// - are there two opposite colored kings?, otherwise returns -2
// - are all specified pieces (!= 0) on different squares, otherwise returns -3
// - is side-not-to-move in check? -> illegal, returns -4
// - if ep_square != 0 and ep_square != SQ_NONE, is en-passant legal on ep_square?, otherwise returns -5
int prophet_tb_is_valid_position(const int pieces[6], const int squares[6], const int stm, const int ep_square);

// for valid positions, prophet_tb_probe_dtm:
// returns  1000 if draw
// returns     0 if checkmate
// returns  1000 > v > 0 if win in v plies
// returns -1000 < v < 0 if loss in v plies
// returns -1001 if table is completely missing (both sides, if one side is missing it performs search)
// for invalid positions: undefined/unsafe behaviour -> may trigger assertions
// this function is thread-safe
int prophet_tb_probe_dtm(const int pieces[6], const int squares[6], const int stm, const int ep_square);

// for better performance reuse prophet_tb_decompress_ctx
// for multi-threading use one prophet_tb_decompress_ctx per thread
// all functions below are thread-safe
typedef struct prophet_tb_decompress_ctx prophet_tb_decompress_ctx;
prophet_tb_decompress_ctx* prophet_tb_create_decompress_ctx();
void prophet_tb_free_decompress_ctx(prophet_tb_decompress_ctx* dctx);
int prophet_tb_probe_dtm_dctx(const int pieces[6], const int squares[6], const int stm, const int ep_square, prophet_tb_decompress_ctx* dctx);


#ifdef __cplusplus
}
#endif

#endif