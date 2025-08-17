
fast:
	g++  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64 -mmacosx-version-min=10.15 -arch arm64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT  -march=armv8.2-a+dotprod    -flto=full   -c -o main.o main.cpp
	g++ -o main bitboard.o main.o movegen.o position.o uci.o -m64 -mmacosx-version-min=10.15 -arch arm64 -lpthread  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64 -mmacosx-version-min=10.15 -arch arm64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT  -march=armv8.2-a+dotprod    -flto=full


build:
	g++  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64 -mmacosx-version-min=10.15 -arch arm64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT  -march=armv8.2-a+dotprod    -flto=full   -c -o bitboard.o bitboard.cpp
	g++  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64 -mmacosx-version-min=10.15 -arch arm64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT  -march=armv8.2-a+dotprod    -flto=full   -c -o movegen.o movegen.cpp
	g++  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64 -mmacosx-version-min=10.15 -arch arm64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT  -march=armv8.2-a+dotprod    -flto=full   -c -o position.o position.cpp
	g++  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64 -mmacosx-version-min=10.15 -arch arm64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT  -march=armv8.2-a+dotprod    -flto=full   -c -o uci.o uci.cpp
	g++  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64 -mmacosx-version-min=10.15 -arch arm64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT  -march=armv8.2-a+dotprod    -flto=full   -c -o main.o main.cpp
	g++ -o main bitboard.o main.o movegen.o position.o uci.o -m64 -mmacosx-version-min=10.15 -arch arm64 -lpthread  -Wall -Wcast-qual -fno-exceptions -std=c++17  -pedantic -Wextra -Wshadow -Wmissing-declarations -m64 -mmacosx-version-min=10.15 -arch arm64  -DNDEBUG -O3 -funroll-loops -DIS_64BIT -DUSE_POPCNT  -march=armv8.2-a+dotprod    -flto=full

