
#-NDEBUG
# -fno-exceptions
# -O3
# -flto=full

flags = -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -m64 -mbmi2  -O2 -funroll-loops -DIS_64BIT -DUSE_POPCNT -fopenmp -O3

fast:
	g++ -g $(flags)  -c -o main.o main.cpp
	g++ -g -o main bitboard.o main.o uci.o  $(flags)


build:
	g++ -g $(flags) -c -o bitboard.o bitboard.cpp
	g++ -g $(flags) -c -o uci.o uci.cpp
	g++ -g $(flags) -c -o main.o main.cpp
	g++ -g -o main bitboard.o main.o uci.o $(flags)

mates:
	g++ -g $(flags)  -c -o longest_mate.o longest_mate.cpp
	g++ -g -o longest_mate bitboard.o longest_mate.o uci.o  $(flags)
