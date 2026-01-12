
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
	$(CC) -g $(flags) -c -o build/bitboard.o src/bitboard.cpp
	$(CC) -g $(flags) -c -o build/compressed_tb.o src/compressed_tb.cpp $(lzstd)
	$(CC) -g $(flags) -c -o build/eg_movegen.o src/eg_movegen.cpp
	$(CC) -g $(flags) -c -o build/eg_position.o src/eg_position.cpp
	$(CC) -g $(flags) -c -o build/egtb.o src/egtb.cpp $(lzstd)
	$(CC) -g $(flags) -c -o build/egtb_ids.o src/egtb_ids.cpp
	$(CC) -g $(flags) -c -o build/kkx.o src/kkx.cpp
	$(CC) -g $(flags) -c -o build/linearize.o src/linearize.cpp
	$(CC) -g $(flags) -c -o build/triangular_indexes.o src/triangular_indexes.cpp
	$(CC) -g $(flags) -c -o build/uci.o src/uci.cpp

corefiles = build/bitboard.o build/compressed_tb.o build/eg_movegen.o build/eg_position.o build/egtb.o build/egtb_ids.o build/kkx.o build/linearize.o build/triangular_indexes.o build/uci.o

gen:
	$(CC) -g $(flags)  -o build/gen_main.out src/gen_main.cpp $(corefiles) $(lzstd)

compress:
	$(CC) -g $(flags) -o build/compress_files.out src/compress_files.cpp $(corefiles) $(lzstd)

prophet: lib
	$(CC) -g $(flags) -shared -o build/libprophet.so src/prophet.cpp $(corefiles) $(lzstd)

lprophet = -I src -L build -lprophet -Wl,-rpath,'$$ORIGIN/build'

mates:
	$(CC) -g $(flags) -o build/longest_mate.out src/longest_mate.cpp $(lprophet) $(lzstd)
	$(CC) -g $(flags) -o build/longest_mate_lines.out src/longest_mate_lines.cpp $(lprophet) $(lzstd)
