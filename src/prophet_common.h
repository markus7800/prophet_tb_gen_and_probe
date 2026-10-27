#ifndef PROPHET_COMMON_H
#define PROPHET_COMMON_H

#include <string>
#include "types.h"
#include "values.h"
#include "eg_position.h"
#include "eg_movegen.h"
#include "egtb.h"
#include "egtb_ids.h"
#include "compressed_tb.h"

// returns v = 0 if draw or illegal position
// returns 0<1000-v if mate in v plies
// return -1000+v<0 if loss in v plies
// returns -1001 if table is missing
int16_t probe_position_raw_dctx(EGPosition &pos, DecompressCtx* dctx);
int16_t probe_position_raw_dtm(EGPosition &pos);

int dtm(int16_t raw);
// probe_position_dtm_dctx = dtm(probe_position_raw_dctx)
int probe_position_dtm_dctx(EGPosition &pos, DecompressCtx* dctx);
int probe_position_dtm(EGPosition &pos);

#define WDL_WIN 1
#define WDL_DRAW 0
#define WDL_LOSS -1
// returns WDL_WIN (1) if side-to-move wins, 0 if not, -1001 if WDL table of side-to-move is missing
int probe_position_win_dctx(EGPosition &pos, DecompressCtx* dctx);
// returns WDL_WIN, WDL_DRAW, WDL_LOSS or -1001 if a required WDL table is missing
int probe_position_wdl_dctx(EGPosition &pos, DecompressCtx* dctx);
int probe_position_wdl(EGPosition &pos);

#endif