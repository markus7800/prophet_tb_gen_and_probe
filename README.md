# Prophet TB: Generator and Probing

This repository contains code to generate and probe chess distance-to-mate (DTM) endgame tablebases.

I was able to generate the tablebase for up to 6 pieces with a Ryzen 9 5950x CPU (16 cores 32 hyper-threads) and 96GB RAM in 14 days.

The 6-men tablebase is block-compressed with 32768 entries per block and is 1111 GiB large.

For now, probing requires mmaping the compressed files and is thus only available on unix-based systems.

A single DTM query takes <0.1ms on my system, where the files are stored on a M.2 SSD.

In the full tablebase, there are two files for each piece configuration, e.g. in KQKNP the side-to-move has a queen against a knight and pawn, and in KNPKQ the side-to-move has knight and pawn against queen. (There is just one file for symmetric configurations, e.g. KRPKRP).

To save space, you may choose to only store one table per piece configuration, which brings size down to 449 GiB.
However, since all moves have to be expanded and probed for positions where the table is now missing, probing takes on average 10 times longer.

## Building

First, you need to build [zstd](https://github.com/facebook/zstd).

```
git submodule update --init
cd zstd
make
cd ..
```


Then, run
```
make prophet
```
to build the shard library `build/prophet.so` used for probing.  
The header file is `src/prophet.h`.

For a demo, build
```
make mates OMP=1
```
and run `build/longest_mates.out [folder] [max_npieces] [threads] [lines]` which finds the longest forced mate for each piece configuration up to `[max_npieces] = 0 | 1 | 2 | 3 | 4` pieces with `[threads]` number of threads.  
`[folder]` is the directory you have your tables stored in.   
By setting `[lines]` to `1` instead of `0`, it also finds the forced move sequence.  
If you do not have OpenMP installed, omit `OMP=1` and use `[threads] = 1`.  
The source file is `src/longest_mates.cpp`.

For generating the tablebase, build
```
make gen OMP=1
```
and run `build/gen_main.out [folder] [max_npieces] [threads]` to compute with `[threads]` number of threads the tables for all piece configuration with a maximum number of `[max_npieces]` pieces.
Results are stored in `[folder]`
You may build with `PDEP=1` to use BMI2's PDEP instruction.
OpenMP is recommended for this computation.

On my 32 thread system, 4-men takes <1 minute and 5-men takes <2 hours to compute. (5-men needs 7 GiB disk space and 16GB RAM).

## Downloads 

To be announced.

If you have downloaded files you can check their integrity with
```
make prophet
make check
./build/check_file_integrity.out [folder] [threads]
```

You have to organise your (sub-)set of tables as shown in the following section.

## Table Size and Structure
```
4.0K    egtbs/2men/0pawns
4.0K    egtbs/2men
48K     egtbs/3men/0pawns
28K     egtbs/3men/1pawns
76K     egtbs/3men
8.4M    egtbs/4men/0pawns
15M     egtbs/4men/1pawns
1.8M    egtbs/4men/2pawns
25M     egtbs/4men
1.7G    egtbs/5men/0pawns
4.0G    egtbs/5men/1pawns
975M    egtbs/5men/2pawns
72M     egtbs/5men/3pawns
6.7G    egtbs/5men
220G    egtbs/6men/0pawns
639G    egtbs/6men/1pawns
213G    egtbs/6men/2pawns
31G     egtbs/6men/3pawns
1.7G    egtbs/6men/4pawns
1.1T    egtbs/6men
1.1T    egtbs
```

For more details see [prophet-tb.com](https://prophet-tb.com/).