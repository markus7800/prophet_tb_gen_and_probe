
# CC = /opt/homebrew/bin/g++-15
CC = g++
flags =  -Wall -Wextra  -std=c++17 -fopenmp -O3
ldeflate = -I libdeflate -L libdeflate/build -l deflate
lzstd = -I zstd/lib -L zstd/lib -lzstd
llz4 = -I lz4/lib -Llz4/lib -l lz4

tzstd:
	$(CC) -g -o compress_zstd.out block_compress_egtb.cpp $(flags) $(lzstd) -DZSTD
tlibdeflate:
	$(CC) -g -o compress_libdeflate.out block_compress_egtb.cpp $(flags) $(ldeflate) -DLIBDEFLATE
tlz4:
	$(CC) -g -o compress_lz4.out block_compress_egtb.cpp $(flags) $(llz4) -DLZ4
	
