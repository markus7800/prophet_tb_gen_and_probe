#include <cassert>
#include <cstdint>
#include <x86intrin.h>

inline uint64_t nthset(uint64_t x, unsigned n) {
    return _pdep_u64(1ULL << n, x);
}

inline uint64_t nthunset(uint64_t x, unsigned n) {
    return _pdep_u64(1ULL << n, ~x);
}

int main() {
    assert(nthset(0b0000'1101'1000'0100'1100'1000'1010'0000, 0) ==
                  0b0000'0000'0000'0000'0000'0000'0010'0000);
    assert(nthset(0b0000'1101'1000'0100'1100'1000'1010'0000, 1) ==
                  0b0000'0000'0000'0000'0000'0000'1000'0000);
    assert(nthset(0b0000'1101'1000'0100'1100'1000'1010'0000, 3) ==
                  0b0000'0000'0000'0000'0100'0000'0000'0000);
    assert(nthset(0b0000'1101'1000'0100'1100'1000'1010'0000, 9) ==
                  0b0000'1000'0000'0000'0000'0000'0000'0000);
    assert(nthset(0b0000'1101'1000'0100'1100'1000'1010'0000, 10) ==
                  0b0000'0000'0000'0000'0000'0000'0000'0000);

}