#ifndef LEGACY_V1_H_INCLUDED
#define LEGACY_V1_H_INCLUDED

// Frozen copy of the original (v1) index and compressed file format, see legacy_v1.cpp.

#include <cstdint>
#include <string>
#include <zstd.h>
#ifdef OMP
#include <omp.h>
#endif
#include "kkx.h"
#include "eg_position.h"
#include "triangular_indexes.h"

namespace v1 {

void compute_poscounts(const int stm_pieces[6], const int sntm_pieces[6], uint64_t kntm_poscounts[], uint64_t& num_nonep_pos, uint64_t& num_ep_pos, uint64_t& num_pos);
void pos_at_ix_(EGPosition &pos, uint64_t ix, Color stm, const int stm_pieces[6], const int sntm_pieces[6], const uint64_t kntm_poscounts[]);
uint64_t ix_from_pos_(EGPosition const &pos, const uint64_t kntm_poscounts[]);

uint64_t compute_checksum(const int16_t* TB, uint64_t num_pos, int nthreads);
// reads a complete v1 .bz file into TB (num_pos entries), verifies size and checksum
bool load_compressed_table(const std::string& filename, uint64_t expected_num_pos, int16_t* TB, int nthreads);

} // namespace v1

#endif
