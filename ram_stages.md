# RAM per stage: current implementation

This covers the branch `optimisations` at step 9 plus step 8: int16 generation, tables not split, and the
finalize tool. For the split by pawn file class and for 1-byte generation, see `ram_requirements.md` on the
branches `optimisations-pawnsplit` and `optimisations-gen-bytes`.

All numbers are computed exactly from the index sizes of every table. 1 GiB = 2³⁰ bytes. "1.5 TB" is 1397 GiB
(decimal) or 1536 GiB (binary).

## Summary: worst table per stage

| Stage | 5-men | 6-men | 7-men |
|---|---|---|---|
| 1. Generation (dependencies + both table sides) | **5.0 GiB** (KPKBP) | **67.5 GiB** (KRBNKP) | **3900 GiB** (KQRBNKP) |
| 2. Consistency check, smallest budget | in memory, no extra | **40.6 GiB** (KPKRBP) | **2494 GiB** (KPKRBNP) |
| 3. Finalize (per file) | **0.67 GiB** (KPKRB) | **40.6 GiB** (KPKRBN) | **2417 GiB** (KPKQRBN) |
| Peak of the whole pipeline | 5.0 GiB | 67.5 GiB | 3900 GiB |

How many jobs need more than a given amount:

| | Generation | Check | Finalize |
|---|---|---|---|
| **5-men** (110 table pairs, 220 files) | > 1 / 2 / 4 / 8 GiB: 41 / 13 / 4 / 0 | 0 | > 1 GiB: 0 |
| **6-men** (365 table pairs, 715 files) | > 16 / 32 / 64 / 90 GiB: 206 / 104 / 32 / 0 | 176 / 100 / 0 / 0 | 249 / 116 / 0 / 0 |
| **7-men** (1001 table pairs, 2002 files) | > 512 / 1024 / 1397 / 2048 / 3072 GiB: 697 / 406 / 350 / 98 / 97 | 506 / 410 / 182 / 130 / 0 | 852 / 391 / 196 / 79 / 0 |

**Conclusion:** 5- and 6-men run comfortably, 6-men on a 96 GB machine as before. 7-men does not fit 1.5 TB
nodes with this branch:

- 350 table pairs need more than 1397 GiB to generate;
- the consistency check needs up to 2.5 TiB, while its budget is hard-coded to 90 GiB here
  (`CONSISTENCY_CHECK_MAX_BYTES`), so the check would stop with "Could not load single EGTB";
- 196 files need more than 1397 GiB to finalize.

The split branch brings generation to at most 1309 GiB and the check to at most 981 GiB.

## What is in memory per stage

### 1. Generation (`GenEGTB::gen`)

Memory: `2·(n_wtm + n_btm) + dependencies`, where n is the number of positions of a table side.

- **The table pair:** both sides, int16, allocated for the whole generation (allocate, promotion retrograde,
  init, retrograde, in-memory check, write).
- **Dependencies, each a complete table side as int16:**
  - captures and capture-promotions, both sides;
  - **with ≤ 5 pieces also the promotion tables.** These dominate at 5 pieces: KPKBP has 4265 MiB of
    dependencies but only 850 MiB of table.
  - From 6 pieces on, promotion tables are read sequentially from the compressed files instead
    (`RETRO_PROMO_MIN_PIECES = 6`). Pawnless tables have no promotions.
- **Writing:** compression streams from the table in memory with about 5 MiB per thread
  (640 MiB with 128 threads), and no second copy of the table.

### 2. Consistency check

- **≤ 5 pieces, and pawnless tables:** done in memory during generation, no extra memory.
- **From 6 pieces with pawns:** done from the compressed files after the table pair has been freed.

  Memory: `dependencies + largest unit + n_side/8`.

  - **Kept:** the capture and capture-promotion tables.
  - **Loaded in rounds within the budget:** the other side of the table and one side of each promotion
    table, each complete. The smallest budget that works is the largest single unit.
  - **Bitmap:** one bit per position of the checked side.
  - **Budget:** fixed at 90 GiB on this branch (configurable with `PROPHET_CHECK_MAX_MIB` on the split
    branch). That is enough for every 6-men table (≤ 40.6 GiB) but no 7-men pawn table.

### 3. Finalize (`finalize.out`, after all generation)

Memory per file: `2·n + capture tables + n/4`.

- **The table side** in canonical order, int16.
- **Its capture tables, complete, int16:** only the tables reached by captures and capture-promotions of the
  side to move, so much less than the generator's dependencies.
- **Two bitmaps:** "used" and "capture don't-care", n/4 bytes, i.e. 1/8 of the table.
- **Small:** estimation and compression buffers (about 5 MiB per thread). The verification decompresses
  block by block and needs no second copy.

### 4. Probing (`prophet`)

Small and independent of the table sizes:

- about 15 KB per table (index offsets);
- about 0.3 MB per decompression context, i.e. per probing thread;
- the files are memory-mapped, so they live in the page cache, which the OS can drop.

## Largest jobs

| | Generation | Check (smallest budget) | Finalize (per file) |
|---|---|---|---|
| **5-men** | KPKBP 5114 MiB (tables 850, dependencies 4265)<br>KPKRP 5094 MiB<br>KPKNP 5022 MiB | – | KPKRB 685 MiB (table 570, capture tables 44)<br>KPKBN 685 MiB<br>KPKRN 685 MiB |
| **6-men** | KRBNKP 67.5 GiB (tables 63.6, dependencies 3.9)<br>KRBKBP 66.8 GiB<br>KQRBKP 66.7 GiB | KPKRBP 40.6 GiB (dependencies 6.2)<br>KPKBNP 40.5 GiB<br>KPKRNP 40.5 GiB | KPKRBN 40.6 GiB (table 32.8, capture tables 3.7)<br>KPKQRB 40.6 GiB<br>KPKQBN 40.5 GiB |
| **7-men** | KQRBNKP 3900 GiB (tables 3616, dependencies 285)<br>KRBNKBP 3872 GiB<br>KRBNKRP 3858 GiB | KPKRBNP 2494 GiB (dependencies 500)<br>KPKQRBP 2486 GiB<br>KPKQBNP 2478 GiB | KPKQRBN 2417 GiB (table 1905, capture tables 273)<br>KKQRBNP 2330 GiB<br>KBPKRBN 2274 GiB |

## Validation (KNPKB, 5-men, 10 threads)

| Stage | Model | Measured |
|---|---|---|
| Generation | 1799 MiB (tables 1099.0, dependencies 699.6) | tables 1099 MiB and dependencies 699 MiB (generator log), 1941 MiB maximum RSS |
| Finalize | 609 MiB (table 524, capture tables ≈ 20, bitmaps 65) | 696 MiB maximum RSS (679 MiB with 1 thread) |

The difference to the measured maximum RSS is fixed process overhead: code, histograms, buffers and layouts,
about 80–150 MiB.
