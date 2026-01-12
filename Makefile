
#-NDEBUG
# -fno-exceptions
# -O3
# -flto=full

CC = g++
flags = -std=c++17 -Wall -Wcast-qual -Wno-unused-command-line-argument -fno-exceptions -pedantic -Wextra -Wshadow -m64 -flto=auto -funroll-loops -DIS_64BIT -DUSE_POPCNT -O3
lzstd = -I zstd/lib -L zstd/lib -lzstd

ifdef OMP
	flags += -fopenmp -DOMP
else
	flags += -Wno-unknown-pragmas
endif
ifdef PDEP
	flags += -mbmi2 -DPDEP
endif

prophet: flags += -fPIC

.PHONY: lib gen mates compress prophet

lib:
	$(CC) -g $(flags) -c -o bitboard.o bitboard.cpp
	$(CC) -g $(flags) -c -o compressed_tb.o compressed_tb.cpp $(lzstd)
	$(CC) -g $(flags) -c -o eg_movegen.o eg_movegen.cpp
	$(CC) -g $(flags) -c -o eg_position.o eg_position.cpp
	$(CC) -g $(flags) -c -o egtb.o egtb.cpp $(lzstd)
	$(CC) -g $(flags) -c -o egtb_ids.o egtb_ids.cpp
	$(CC) -g $(flags) -c -o kkx.o kkx.cpp
	$(CC) -g $(flags) -c -o linearize.o linearize.cpp
	$(CC) -g $(flags) -c -o triangular_indexes.o triangular_indexes.cpp
	$(CC) -g $(flags) -c -o uci.o uci.cpp

corefiles = bitboard.o compressed_tb.o eg_movegen.o eg_position.o egtb.o egtb_ids.o kkx.o linearize.o triangular_indexes.o uci.o

gen:
	$(CC) -g $(flags)  -o gen_main.out gen_main.cpp $(corefiles) $(lzstd)

compress:
	$(CC) -g $(flags) -o compress_files.out compress_files.cpp $(corefiles) $(lzstd)

prophet: lib
	$(CC) -g $(flags) -shared -o build/libprophet.so prophet.cpp $(corefiles) $(lzstd)

lprophet = -I . -L build -lprophet -Wl,-rpath,'$$ORIGIN/build'

mates:
	$(CC) -g $(flags) -o longest_mate.out longest_mate.cpp $(lprophet) $(lzstd)
	$(CC) -g $(flags) -o longest_mate_lines.out longest_mate_lines.cpp $(lprophet) $(lzstd)
