
#-NDEBUG
# -fno-exceptions
# -O3
# -flto=full

CC = g++
flags = -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -m64 -mbmi2 -flto -funroll-loops -DIS_64BIT -DUSE_POPCNT -fopenmp -O3
lzstd = -I zstd/lib -L zstd/lib -lzstd

fast:
	$(CC) -g $(flags)  -c -o main.o main.cpp
	g++ -g -o main.out bitboard.o main.o uci.o  $(flags)

build:
	$(CC) -g $(flags) -c -o bitboard.o bitboard.cpp
	$(CC) -g $(flags) -c -o uci.o uci.cpp
	$(CC) -g $(flags) -c -o main.o main.cpp
	$(CC) -g -o main.out bitboard.o main.o uci.o $(flags)

mates:
	$(CC) -g $(flags) -c -o bitboard.o bitboard.cpp
	$(CC) -g $(flags) -c -o eg_position.o eg_position.cpp
	$(CC) -g $(flags) -c -o eg_movegen.o eg_movegen.cpp
	$(CC) -g $(flags) -c -o linearize.o linearize.cpp
	$(CC) -g $(flags) -c -o triangular_indexes.o triangular_indexes.cpp
	$(CC) -g $(flags) -c -o kkx.o kkx.cpp
	$(CC) -g $(flags) -c -o uci.o uci.cpp
	$(CC) -g $(flags) -c -o egtb_ids.o egtb_ids.cpp
	$(CC) -g $(flags) -c -o egtb.o egtb.cpp
	$(CC) -g -o longest_mate.out longest_mate.cpp egtb_ids.o egtb.o kkx.o linearize.o triangular_indexes.o eg_position.o eg_movegen.o bitboard.o uci.o  $(flags) $(lzstd) -DZSTD
	$(CC) -g -o longest_mate_lines.out longest_mate_lines.cpp egtb_ids.o egtb.o kkx.o linearize.o triangular_indexes.o eg_position.o eg_movegen.o bitboard.o uci.o  $(flags) $(lzstd) -DZSTD


compress:
	$(CC) -g -o compress_zstd.out block_compress_egtb.cpp $(flags) $(lzstd) -DZSTD
