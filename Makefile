
#-NDEBUG
# -fno-exceptions
# -O3
# -flto=full

CC = g++
flags = -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -m64 -mbmi2 -funroll-loops -DIS_64BIT -DUSE_POPCNT -fopenmp -O3
lzstd = -I zstd/lib -L zstd/lib -lzstd

fast:
	$(CC) -g $(flags)  -c -o main.o main.cpp
	g++ -g -o main bitboard.o main.o uci.o  $(flags)

build:
	$(CC) -g $(flags) -c -o bitboard.o bitboard.cpp
	$(CC) -g $(flags) -c -o uci.o uci.cpp
	$(CC) -g $(flags) -c -o main.o main.cpp
	$(CC) -g -o main bitboard.o main.o uci.o $(flags)

mates:
	$(CC) -g $(flags)  -c -o longest_mate.o longest_mate.cpp
	$(CC) -g -o longest_mate bitboard.o longest_mate.o uci.o  $(flags)


compress:
	$(CC) -g -o compress_zstd.out block_compress_egtb.cpp $(flags) $(lzstd) -DZSTD
