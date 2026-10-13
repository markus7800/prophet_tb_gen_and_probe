# Full 5-men run on a new machine

These commands build everything, generate all tables up to 5 men, check them, and finalize them for distribution
(piece order and capture don't-cares).

**What to expect:**
- **RAM:** at most about 5.2 GiB (KPKBP), plus about 5 MiB per thread. See `ram_stages.md`.
- **Disk:** about 6 GB for the generated set. With `--out`, finalize writes a second set that is expected to be
  15–30% smaller.
- **Time:** under 2 hours on 32 threads (README), and about 28% faster since step 5.

## 1. Prerequisites

On Linux, GCC brings OpenMP with it. On macOS, run `brew install libomp` instead.

```bash
sudo apt-get install -y git g++ make
```

## 2. Get the code (branch `optimisations`)

Replace `<repo-url>` with the URL of the repository.

```bash
git clone <repo-url> prophet && cd prophet
```

```bash
git checkout optimisations
```

```bash
git submodule update --init
```

```bash
make -C zstd/lib -j libzstd.a
```

## 3. Build

`prophet` comes first, because the tools link against `build/libprophet.so`. On x86 CPUs with fast BMI2 you can add
`PDEP=1`, but not on AMD before Zen 3, where PDEP is slow.

```bash
make prophet OMP=1
```

```bash
make gen OMP=1
```

```bash
make unittest compare check finalize mates OMP=1
```

## 4. Unit tests

About 1–3 minutes. If the old published tables are available, add their folder as a second argument; that also
tests reading old-format files.

```bash
build/unit_tests.out /tmp/prophet_unit
```

## 5. Generate all tables up to 5 men

The `3` means 3 pieces besides the kings. `/usr/bin/time -v` reports the peak memory ("Maximum resident set size").

```bash
/usr/bin/time -v build/gen_main.out egtbs 3 $(nproc) 2>&1 | tee gen_5men.log
```

Timing summary from the log:

```bash
grep -E "^Finished (init|retrograde) in|^Finished in-memory|^Wrote to disk|^Finished TBs" gen_5men.log | awk '{s[$1" "$2]+=$NF} END {for (k in s) print k, s[k] "s"}'
```

## 6. Check the generated tables

Checksums and file structure:

```bash
build/check_file_integrity.out egtbs $(nproc)
```

Exhaustive comparison with old-format tables in a folder `ref`: either the published tables (for example
downloaded from prophet-tb.com and unpacked), or the output of the baseline run in section 8
(`../prophet-old/egtbs_old`). This compares every used position and probes 100k random positions per table.

```bash
build/compare_tables.out ref egtbs $(nproc) 100000
```

Longest mates, run on the tables before finalizing:

```bash
build/longest_mates.out egtbs 3 $(nproc) 0
```

## 7. Finalize (piece order and capture don't-cares)

`--out` writes a complete second set and leaves `egtbs` unchanged. Without `--out`, the files are replaced in
place, and they can then no longer serve as dependencies for generating 6-men.

```bash
/usr/bin/time -v build/finalize.out egtbs $(nproc) --out egtbs_final 2>&1 | tee finalize_5men.log
```

```bash
build/check_file_integrity.out egtbs_final $(nproc)
```

This checks the don't-care rule and probes through the finalized tables:

```bash
build/compare_tables.out ref egtbs_final $(nproc) 100000
```

```bash
du -sh egtbs egtbs_final
```

## 8. Baseline: the old generator, for a timing comparison

The README figure (5-men in under 2 hours) was measured on a Ryzen 5950X with 32 threads, and the table files
contain no timings. For a fair comparison, run the old generator once on the same machine, with the same number of
threads and no other load. As a side effect it produces a complete set of old-format tables, which serves as `ref`
for `compare_tables` in sections 6 and 7.

`3edbbcb` is the last commit before the optimisations (`main` at the time). Both versions build the same way, use
zstd level 19 and blocks of 32768 values, and run the same consistency checks. The old generator has no
target-table mode and no finalize, so compare the generation only; finalize time is in `finalize_5men.log`.

From the `prophet` folder, check out the old code next to it:

```bash
git worktree add ../prophet-old 3edbbcb
```

```bash
cd ../prophet-old && git submodule update --init && make -C zstd/lib -j libzstd.a && make gen OMP=1
```

```bash
/usr/bin/time -v build/gen_main.out egtbs_old 3 $(nproc) 2>&1 | tee gen_5men_old.log
```

The old log has the same timing lines, so the same summary works. "Wrote to disk" includes the in-memory
consistency check in both versions, so the sums are comparable as they are; "Finished TBs" is the total per table
pair.

```bash
grep -E "^Finished (init|retrograde) in|^Finished in-memory|^Wrote to disk|^Finished TBs" gen_5men_old.log | awk '{s[$1" "$2]+=$NF} END {for (k in s) print k, s[k] "s"}'
```

Back in `prophet`, compare the new tables against the baseline's:

```bash
cd ../prophet && build/compare_tables.out ../prophet-old/egtbs_old egtbs $(nproc) 100000
```

A shorter comparison is up to 4 men (`2` instead of `3` in both runs, a minute or two each), but it is not
representative: the gains are largest for 5- and 6-men tables.

## Notes

- **Output layout:** tables go to `egtbs/<n>men/<p>pawns/`. The run is resumable: rerunning `gen_main` skips tables
  whose files already exist, and `finalize` skips files it has already finalized.
- **Two settings are compiled in:** zstd level 19 and blocks of 32768 values. Change them with, for example,
  `make gen OMP=1 ZSTD_LEVEL=15`; finalize takes `--level`.
- **Quick checks during development:** `scripts/verify.sh quick` (up to 4 men) and `scripts/verify.sh big` (KNPKB
  and everything it depends on). Both compare against the old tables in `~/prophet_egtbs/egtbs_32768`, which can be
  changed with `REF=…`.
