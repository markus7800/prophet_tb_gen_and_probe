
#-NDEBUG
# -fno-exceptions
# -O3
# -flto=full

CC = g++
flags = -std=c++17 -Wall -Wcast-qual -Wno-unused-command-line-argument -fno-exceptions -pedantic -Wextra -Wshadow -m64 -flto=auto -funroll-loops -DIS_64BIT -DUSE_POPCNT -O3
lzstd = -I zstd/lib zstd/lib/libzstd.a

ifdef OMP
ifeq ($(shell uname -s),Darwin)
	# Apple clang has no built-in OpenMP; use Homebrew's libomp
	LIBOMP ?= $(shell brew --prefix libomp)
	flags += -Xpreprocessor -fopenmp -I$(LIBOMP)/include -L$(LIBOMP)/lib -lomp -DOMP
else
	flags += -fopenmp -DOMP
endif
else
	flags += -Wno-unknown-pragmas
endif
ifdef BLOCKSIZE
	flags += -DEGTB_BLOCKSIZE=$(BLOCKSIZE)
endif
ifdef ZSTD_LEVEL
	flags += -DEGTB_COMPRESSION_LEVEL=$(ZSTD_LEVEL)
endif
ifdef MAX_DICT
	# only for testing the escape code path of the rank encoding
	flags += -DEGTB_MAX_DICT=$(MAX_DICT)
endif
ifdef PDEP
	flags += -mbmi2 -DPDEP
endif

prophet: flags += -fPIC

.PHONY: lib gen mates compress recompress prophet check test compare unittest clean

build:
	mkdir -p build

clean:
	rm -rf build

lib: | build
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

gen: lib | build
	$(CC) -g $(flags) -o build/gen_main.out src/gen_main.cpp $(corefiles) $(lzstd)

compress: | build
	$(CC) -g $(flags) -o build/compress_files.out src/compress_files.cpp $(corefiles) $(lzstd)

recompress: | build
	$(CC) -g $(flags) -o build/recompress.out src/recompress.cpp src/compressed_tb.cpp $(lzstd)

prophet: lib | build
	$(CC) -g $(flags) -shared -o build/libprophet.so src/prophet.cpp $(corefiles) $(lzstd)

lprophet = -I src -L build -lprophet -Wl,-rpath,'$$ORIGIN'

check: | build
	$(CC) -g $(flags) -o build/check_file_integrity.out src/check_file_integrity.cpp $(lprophet) $(lzstd)

mates: | build
	$(CC) -g $(flags) -o build/longest_mates.out src/longest_mates.cpp $(lprophet) $(lzstd)

test: | build
	$(CC) -g $(flags) -o build/test.out src/test.cpp $(lprophet) $(lzstd)

compare: | build
	$(CC) -g $(flags) -o build/compare_tables.out src/compare_tables.cpp src/legacy_v1.cpp $(lprophet) $(lzstd)

unittest: | build
	$(CC) -g $(flags) -o build/unit_tests.out src/unit_tests.cpp src/legacy_v1.cpp $(lprophet) $(lzstd)
