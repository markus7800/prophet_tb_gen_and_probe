# RAM required to generate 5-, 6- and 7-men tables

RAM needed per generation job, with and without splitting 1–2-pawn tables by pawn file class (step 7).

All numbers are computed exactly from the index sizes (`TableIndex`) of every table, following what the
generator keeps in memory. 1 GiB = 2³⁰ bytes. A "1.5 TB" node has 1397 GiB if decimal (1536 GiB if binary).

To reproduce, run the following; table ids among the arguments list their jobs, e.g.
`build/ram_requirements.out 7 KRBPKPP`.

```
make prophet OMP=1 && make ram OMP=1
build/ram_requirements.out
```

## Summary

| | Without split | With split | With split + 1-byte generation¹ |
|---|---|---|---|
| **5-men** (110 tables) | 5.0 GiB (KPKBP) | 4.2 GiB (KPKBP part) | 4.2 GiB |
| **6-men** (365 tables) | 67.5 GiB (KRBNKP) | 21.8 GiB (KRBKRB) | 16.9 GiB (KKQRBN) |
| **7-men** (1001 tables) | **3900 GiB** (KQRBNKP) | **1309 GiB** (KRBPKPP) | **1049 GiB** (KRBPKPP) |
| 7-men jobs above 1.5 TB (1397 GiB) | 378 of 1001 | 0 of 3971 | 0 of 3971 |

Each value is the peak of the worst job: the maximum over generation and consistency check, including the
dependency tables held in memory. Process overhead comes on top: about 150 MiB measured, plus the OS.

¹ The `optimisations-gen-bytes` branch (step 6, not on the main line), applied on top of the split.

## What is in memory

**A job** is one table pair: white-to-move and black-to-move.
- **Without split:** the whole pair.
- **With split:** one part of the white-to-move table, together with the part of the black-to-move table
  that has the same pawn classes (colours swapped). Split tables have 4, 10 or 16 parts.
- Pawnless tables and tables with 3 or more pawns are never split.

**Generation:**
- both sides of the job's table (part) at 2 bytes per position;
- dependency tables, always loaded completely at 2 bytes per position:
  - captures and capture-promotions;
  - for tables with ≤ 5 pieces also the promotion tables (`RETRO_PROMO_MIN_PIECES = 6`). From 6 pieces on,
    promotions are read sequentially from the compressed files and need no table memory.

**Consistency check:**
- **Up to 5 pieces:** done in memory during generation, no extra memory.
- **From 6 pieces:** done after writing, from the compressed files. It keeps the capture tables and
  capture-promotion tables in memory and loads, round by round within the budget `PROPHET_CHECK_MAX_MIB`:
  - the other side of the table;
  - one side of each promotion table.

  The values below are the **smallest budget that works**: dependencies plus the largest single unit.
  - **Without split** (the old check), a unit is a complete table side.
  - **With split** (part-aware check), a unit is one part. Only the parts that moves from the job's part can
    reach are loaded: the paired part of the other side, and the promotion parts with compatible pawn classes.

  A larger budget means fewer rounds (less time), not a different result.

**1-byte generation** (branch): int16 for one side during init, then 1 byte per position:
`deps + max(2·n_btm, n_btm + 2·n_wtm)` bytes, where n is a part's number of positions.

## 5-men

| | Without split | With split |
|---|---|---|
| Jobs | 110 | 350 |
| Largest table pair (both sides) | 1124 MiB (KKRBP) | 373 MiB (KKRBN, not split) |
| Worst generation | 5114 MiB (KPKBP: tables 850, dependencies 4265) | 4320 MiB (KPKBP part: tables 55, dependencies 4265) |
| Consistency check | in memory (0 extra) | in memory (0 extra) |
| Jobs above 2 / 4 / 8 GiB | 13 / 4 / 0 | 145 / 64 / 0 |

At 5 pieces, memory is dominated by the **promotion tables**, which are loaded completely: KPKBP needs
8 promotion tables. So the split barely helps here. The jobs above 2 GiB increase with the split because
every part needs the same dependencies.

Generating with the 6-men code path (`make gen RETRO_PROMO_MIN_PIECES=5`) removes the promotion tables from
memory. For example, a KNPKB part then needs 308 MiB instead of 974 MiB, at the cost of a slower promotion
retrograde.

## 6-men

| | Without split | With split |
|---|---|---|
| Jobs | 365 | 1325 |
| Largest table pair | 63.7 GiB (KKRBNP) | 21.0 GiB (KRBKRB, pawnless, not split) |
| Worst generation | 67.5 GiB (KRBNKP: tables 63.6, dependencies 3.9) | 21.8 GiB (KRBKRB: tables 21.0, dependencies 0.7) |
| Worst check, smallest budget | 39.1 GiB (KPKRBP) | 14.5 GiB (KPKRBP part) |
| Jobs above 16 / 32 / 64 / 90 GiB | 214 / 132 / 32 / 0 | 270 / 0 / 0 / 0 |

Top jobs:
- **without split:** KRBNKP 67.5, KRBKBP 66.8, KQRBKP 66.7, KRBKRP 66.5, KBKRBP 66.1 GiB
- **with split:** KRBKRB 21.8, KBKRBN 21.7, KKQRBN 21.6, KRKRBN 21.6, KRBKBN 21.5 GiB (all pawnless)

With the split, the largest jobs are pawnless tables. 6-men can then be generated on a 32 GB machine,
with a check budget of at least 15 GiB.

## 7-men

| | Without split | With split |
|---|---|---|
| Jobs | 1001 | 3971 |
| Largest table pair | 3646 GiB (KRBKRBP) | 1188 GiB (KRBKRBN, pawnless, not split) |
| Worst generation | 3900 GiB (KQRBNKP: tables 3616, dependencies 285) | 1309 GiB (KRBPKPP: tables 1061, dependencies 249) |
| Worst check, smallest budget | 2405 GiB (KPKRBNP) | 981 GiB (KPKRBNP part: dependencies 500 + one part) |
| Largest dependencies | 500 GiB (KPKRBNP) | 500 GiB (KPKRBNP) |
| Jobs above 256 / 512 / 1024 GiB | 838 / 737 / 430 | 3383 / 2087 / 327 |
| Jobs above 1300 / 1397 / 2048 GiB | 378 / 378 / 121 | 1 / 0 / 0 |

Top jobs:
- **without split:** KQRBNKP 3900, KRBNKBP 3872, KRBNKRP 3858, KRBKRBP 3850, KQRBKBP 3825 GiB
- **with split:** KRBPKPP 1309, KBNPKPP 1292, KRNPKPP 1287, KQBPKPP 1276, KQRPKPP 1271 GiB
- **with split + 1-byte generation:** KRBPKPP 1049 GiB at most; 3 jobs above 1024 GiB

With the split, the largest jobs are **3-pawn tables**, which are not split. Their tables need up to
1061 GiB, plus 249 GiB of dependencies.

## Conclusions for 7-men on 1.5 TB nodes

- **Without the split, 7-men does not fit:** 378 tables need more than 1.5 TB, up to 3.9 TiB.
- **With the split, every job fits.** The worst is KRBPKPP at 1309 GiB (one job above 1300 GiB). That
  leaves about 90 GiB of a 1397 GiB node for the OS and process. This is tight but feasible; on a 1536 GiB
  node the margin is about 230 GiB.
- **Consistency check budget:** set `PROPHET_CHECK_MAX_MIB` to at least 981 GiB (`1004544`), and ideally close
  to the usable RAM so the check needs few rounds. The check runs after the generation memory has been
  freed, so it doesn't add to the generation peak.
- **More headroom, if needed:**
  - 1-byte generation (branch): worst job 1049 GiB;
  - splitting 3-pawn tables as well: the largest remaining tables are then pawnless (1188 GiB +
    dependencies);
  - loading dependency tables as 1-byte ranks (halves the 250–500 GiB of dependencies).
- **Several jobs per node:** 3383 of the 3971 jobs need more than 256 GiB, so in practice one large job per
  node. Smaller tables (and the many small parts) can share a node.

## Validation against measurements (KNPKB, 5-men)

| | Model | Measured |
|---|---|---|
| Tables, without split (int16) | 1099.0 MiB | 1099 MiB (generator log, step 6 measurement) |
| Dependencies (all loaded, ≤ 5 pieces) | 699.6 MiB | 699 MiB (generator log) |
| Generation, without split | 1798.6 MiB | 1941 MiB max RSS (includes process overhead) |
| One part with `RETRO_PROMO_MIN_PIECES=5` | 274 + 34 = 308 MiB | 308 MiB (generator log, step 7) |
