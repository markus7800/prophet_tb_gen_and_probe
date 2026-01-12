#ifndef PROPHET_H
#define PROPHET_H

#define NO_OMP

#include <string>
#include "types.h"
#include "values.h"
#include "eg_position.h"
#include "eg_movegen.h"
#include "egtb.h"
#include "egtb_ids.h"
#include "compressed_tb.h"

extern std::unordered_map<std::string, EGTB*> id_to_egtb;

void init_prophet_tb(std::string path);
void deinit_prophet_tb();

// returns v=0 if draw or illegal position
// returns  1000-v if win in v plies
// returns -1000+v if loss in v plies
int16_t probe(Piece pieces[6], Square squares[6]);

// for better performance reuse DecompressCtx
// for multi-threading use one DecompressCtx per thread
DecompressCtx* CreateDecompressCtx();
int16_t probe_dctx(Piece pieces[6], Square squares[6], DecompressCtx* dctx);


int16_t probe_position(EGPosition pos);
int16_t probe_position_dctx(EGPosition pos, DecompressCtx* dctx);

int dtm(int16_t v) {
    if (v == 0) {
        return 0;
    } else if (v > 0) {
        return WIN_IN(0) - v;
    } else {
        return v - LOSS_IN(0);
    }
}
#endif