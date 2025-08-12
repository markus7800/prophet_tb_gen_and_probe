#include <iostream>
#include "perft.h"
#include "bitboard.h"
#include "position.h"

int main() {
    Stockfish::Bitboards::init();
    Stockfish::Position::init();
    Stockfish::Benchmark::perft("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 7, false);
    return 0;
}
