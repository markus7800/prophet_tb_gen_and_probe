
# CC = /opt/homebrew/bin/g++-15
CC = g++
flags =  -Wall -Wextra  -std=c++17 -fopenmp -O2
deflate = -I libdeflate -L libdeflate/build -l deflate
repair:
	$(CC) -g -c  block_compress_egtb.cpp repair.cpp encoder.cpp huffman.cpp $(flags) $(deflate)
	$(CC) -g -o block_compress_egtb.out block_compress_egtb.o repair.o encoder.o huffman.o $(flags) $(deflate)