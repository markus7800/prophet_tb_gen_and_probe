
#ifndef LINEARIZE_H_INCLUDED
#define LINEARIZE_H_INCLUDED

#include "kkx.h"
#include "eg_position.h"
#include "uci.h"
#include "triangular_indexes.h"

void compute_poscounts(const int stm_pieces[6], const int sntm_pieces[6], uint64_t kntm_poscounts[], uint64_t& num_nonep_pos, uint64_t& num_ep_pos, uint64_t& num_pos);

void pos_at_ix_(EGPosition &pos, uint64_t ix, Color stm, const int stm_pieces[6], const int sntm_pieces[6], const uint64_t kntm_poscounts[]);

uint64_t ix_from_pos_(EGPosition const &pos, const uint64_t kntm_poscounts[]);

void transform_to_canoncial(const EGPosition &pos, EGPosition &pos2);

void transform_to(const EGPosition &pos, EGPosition &pos2, int8_t h_flip, int8_t v_flip, int8_t swap);

#endif