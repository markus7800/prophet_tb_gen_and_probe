
fast:
	clang++  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT     -flto=full   -c -o main.o main.cpp
	clang++ -o main bitboard.o main.o movegen.o position.o uci.o -m64 -lpthread  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT     -flto=full


build:
	clang++  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT     -flto=full   -c -o bitboard.o bitboard.cpp
	clang++  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT     -flto=full   -c -o movegen.o movegen.cpp
	clang++  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT     -flto=full   -c -o position.o position.cpp
	clang++  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT     -flto=full   -c -o uci.o uci.cpp
	clang++  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT     -flto=full   -c -o main.o main.cpp
	clang++ -o main bitboard.o main.o movegen.o position.o uci.o -m64 -lpthread  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT     -flto=full

