#!/bin/bash
# Generates tables with the current code and compares them exhaustively against reference tables.
#
#   scripts/verify.sh quick   all tables up to 4 pieces
#   scripts/verify.sh big     KNPKB and all tables it depends on (up to 5 pieces)
#
# Environment variables:
#   REF      reference tables (v1 format), default ~/prophet_egtbs/egtbs_32768
#   OUT      output folder (deleted first), default /tmp/prophet_verify_<mode>
#   THREADS  default: number of CPUs
#   SAMPLES  random prophet probes per table, default 100000
#   MAKEFLAGS_EXTRA  extra make variables, e.g. "ZSTD_LEVEL=19"
set -euo pipefail

MODE=${1:-quick}
REF=${REF:-$HOME/prophet_egtbs/egtbs_32768}
OUT=${OUT:-/tmp/prophet_verify_$MODE}
THREADS=${THREADS:-$(getconf _NPROCESSORS_ONLN)}
SAMPLES=${SAMPLES:-100000}
MAKEFLAGS_EXTRA=${MAKEFLAGS_EXTRA:-}

cd "$(dirname "$0")/.."

case $MODE in
    quick) NPIECES=2; TARGET="" ;;
    big)   NPIECES=3; TARGET="KNPKB" ;;
    *) echo "unknown mode $MODE (quick|big)"; exit 2 ;;
esac

echo "== build"
make prophet OMP=1 $MAKEFLAGS_EXTRA > /dev/null
make gen OMP=1 $MAKEFLAGS_EXTRA > /dev/null
make compare OMP=1 $MAKEFLAGS_EXTRA > /dev/null

echo "== generate ($MODE) into $OUT"
rm -rf "$OUT"
mkdir -p "$OUT"
LOG="$OUT/gen.log"
START=$(date +%s)
build/gen_main.out "$OUT" $NPIECES "$THREADS" $TARGET > "$LOG" 2>&1
END=$(date +%s)

echo "== timings (summed over all tables)"
# "Wrote to disk in" is measured from the end of the retrograde analysis, so it includes the consistency
# check if that runs before writing (in-memory check); subtract it to report pure compression time
awk '
    /^Finished allocate and load in/      { load += $(NF) + 0 }
    /^Finished init in/                   { init += $(NF) + 0 }
    /^Finished retrograde in/             { retro += $(NF) + 0; written = 0; pending = 0 }
    /^Finished in-memory consistency/     { check += $(NF) + 0; if (!written) pending += $(NF) + 0 }
    /^Wrote to disk in/                   { write += $(NF) - pending; written = 1 }
    /^Finished TBs/                       { total += $(NF) + 0; n++ }
    END {
        printf "tables generated:   %d\n", n
        printf "allocate+load:      %8.1fs\n", load
        printf "init:               %8.1fs\n", init
        printf "retrograde:         %8.1fs\n", retro
        printf "compress+write:     %8.1fs\n", write
        printf "consistency checks: %8.1fs\n", check
        printf "total (per table):  %8.1fs\n", total
    }' "$LOG"
echo "wall time:          $((END - START))s"
echo "disk usage:         $(du -sh "$OUT" | cut -f1)"

echo "== compare against $REF"
build/compare_tables.out "$REF" "$OUT" "$THREADS" "$SAMPLES" | tee "$OUT/compare.log" | grep -v ": OK" || true
grep -q " 0 FAILED" "$OUT/compare.log"
