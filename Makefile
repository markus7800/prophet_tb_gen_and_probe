
#-NDEBUG
# -fno-exceptions
# -O3
# -flto=full

CC = g++
flags = -std=c++17 -Wall -Wcast-qual -fno-exceptions -pedantic -Wextra -Wshadow -m64 -mbmi2 -flto=auto -funroll-loops -DIS_64BIT -DUSE_POPCNT -fopenmp -O3
lzstd = -I zstd/lib -L zstd/lib -lzstd

core:
	$(CC) -g $(flags) -c -o bitboard.o bitboard.cpp
	$(CC) -g $(flags) -c -o compressed_tb.o compressed_tb.cpp
	$(CC) -g $(flags) -c -o eg_movegen.o eg_movegen.cpp
	$(CC) -g $(flags) -c -o eg_position.o eg_position.cpp
	$(CC) -g $(flags) -c -o egtb.o egtb.cpp
	$(CC) -g $(flags) -c -o egtb_ids.o egtb_ids.cpp
	$(CC) -g $(flags) -c -o kkx.o kkx.cpp
	$(CC) -g $(flags) -c -o linearize.o linearize.cpp
	$(CC) -g $(flags) -c -o triangular_indexes.o triangular_indexes.cpp
	$(CC) -g $(flags) -c -o uci.o uci.cpp

corefiles = bitboard.o compressed_tb.o eg_movegen.o eg_position.o egtb.o egtb_ids.o kkx.o linearize.o triangular_indexes.o uci.o

gen:
	$(CC) -g $(flags) -o gen_main.out gen_main.cpp $(corefiles) $(lzstd) -DZSTD

mates:
	$(CC) -g $(flags) -o longest_mate.out longest_mate.cpp $(corefiles) $(lzstd) -DZSTD
	$(CC) -g $(flags) -o longest_mate_lines.out longest_mate_lines.cpp $(corefiles) $(lzstd) -DZSTD

compress:
	$(CC) -g $(flags) -o compress_files.out compress_files.cpp $(corefiles) $(lzstd) -DZSTD
