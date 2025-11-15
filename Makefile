
#-NDEBUG
# -fno-exceptions
# -O3
# -flto=full
fast:
	g++ -g -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -m64 -mbmi2  -O2 -funroll-loops -DIS_64BIT -DUSE_POPCNT       -fopenmp -c -o main.o main.cpp
	g++ -g -o main bitboard.o main.o uci.o -m64 -mbmi2 -lpthread  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -m64 -mbmi2  -DNDEBUG -O2 -funroll-loops -DIS_64BIT -DUSE_POPCNT      -fopenmp


build:
	g++ -g -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -m64 -mbmi2   -O2 -funroll-loops -DIS_64BIT -DUSE_POPCNT      -fopenmp  -c -o bitboard.o bitboard.cpp
	g++ -g -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -m64 -mbmi2  -O2 -funroll-loops -DIS_64BIT -DUSE_POPCNT      -fopenmp   -c -o uci.o uci.cpp
	g++ -g -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -m64 -mbmi2  -O2 -funroll-loops -DIS_64BIT -DUSE_POPCNT       -fopenmp   -c -o main.o main.cpp
	g++ -g -o main bitboard.o main.o uci.o -m64 -mbmi2 -lpthread  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -m64 -mbmi2  -DNDEBUG -O2 -funroll-loops -DIS_64BIT -DUSE_POPCNT    -fopenmp

