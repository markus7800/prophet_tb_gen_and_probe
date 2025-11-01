#
#ifndef TRIL_IX_H_INCLUDED
#define TRIL_IX_H_INCLUDED

#include <cstdint>
#include <iostream>
#include <cassert>


uint64_t _number_of_ordered_tuples(uint64_t n_domain, uint64_t n_tuple) {
    uint64_t res = 1;
    uint64_t f = 1;
    for (uint64_t i = 0; i < n_tuple; i++) {
        res *= (n_domain - i);
        f *= i + 1;
    }
    return res / f;
}

uint64_t NUMBER_OF_ORDER_TUPLES[65][8];
uint64_t NUMBER_OF_ORDER_TUPLES_WITH_FIRST_PAWN[25][8];

inline int pawnix24_to_sqix48(int ix) {
    return ix + (ix >> 2) * 4; // put pawn on left side of board, map tp 0,1,2,3,4,8,9,10,11,16,17,...
}

// sq_ix in 0,1,...,48
inline int sqix48_to_pawnix24(int sq_ix) {
    return sq_ix - (sq_ix >> 3) * 4; // map to 0,1,...,23
}

void init_tril() {
    for (uint64_t i = 0; i <= 64; i++) {
        for (uint64_t n = 0; n < 8; n++) {
            NUMBER_OF_ORDER_TUPLES[i][n] = _number_of_ordered_tuples(i, n);
        }
    }
    for (uint64_t n = 0; n < 8; n++) {
        uint64_t p = 0;
        for (int i = 0; i <= 24; i++) {
            NUMBER_OF_ORDER_TUPLES_WITH_FIRST_PAWN[i][n] = p;
            int sq_ix = pawnix24_to_sqix48(i);
            p += NUMBER_OF_ORDER_TUPLES[48-sq_ix-1][n];
        }
    }
}

// = n_domain * (n_domain - 1) * (n_domain - 2) ... * (n_domain - n_tuple + 1) / factorial(n_tuple)
// = (n_domain choose n_tuple)
uint64_t number_of_ordered_tuples(uint64_t n_domain, uint64_t n_tuple) {
    return NUMBER_OF_ORDER_TUPLES[n_domain][n_tuple];
}

// = sum((48 - pawn_ix - 1 choose n_tuple - 1) for pawn_ix = 0...23)
uint64_t number_of_ordered_tuples_with_first_pawn(uint64_t n_tuple) {
    assert(n_tuple > 0);
    return NUMBER_OF_ORDER_TUPLES_WITH_FIRST_PAWN[24][n_tuple-1];
}

// ixs in ascending order
uint64_t tril_to_linear(uint64_t n_tuple, int* ixs) {
    /*
    for ixs = ..., i, j, k
    tril_ix = k + (j)(j-1)/2 + (i)(i-1)(i-2)/6 ...
    */
    uint64_t tril_ix = 0;
    for (uint64_t i = 0; i < n_tuple; i++) {
        tril_ix += NUMBER_OF_ORDER_TUPLES[ixs[i]][i+1];
    }
    return tril_ix;
}

// writes to ixs in ascending order
void tril_from_linear(uint64_t n_tuple, uint64_t tril_ix, int* ixs) {
    for (uint64_t j = 0; j < n_tuple - 1; j++) {
        uint64_t s = 0;
        int i = 0;
        while (s <= tril_ix) {
            i++;
            s += NUMBER_OF_ORDER_TUPLES[i][n_tuple-j-1];
        }
        tril_ix -= NUMBER_OF_ORDER_TUPLES[i][n_tuple-j];
        ixs[n_tuple-j-1] = i;
    }
    ixs[0] = tril_ix;
}

// pawn_ix in 0,1,2,3,4,8,9,10,11,16,17,...
// ixs[0] in pawn_ix+1,...,48
// ixs[i+1] in ixs[i]+1,...,48
uint64_t pawn_tril_to_linear(uint64_t n_tuple, int pawn_ix, int* ixs) {
    uint64_t i = sqix48_to_pawnix24(pawn_ix);
    uint64_t tril_ix = NUMBER_OF_ORDER_TUPLES_WITH_FIRST_PAWN[i][n_tuple-1];
    if (n_tuple > 1) {
        for (uint64_t j = 0; j < n_tuple - 1; j++) {
            ixs[j] -= (pawn_ix + 1);
        }
        tril_ix += tril_to_linear(n_tuple - 1, ixs);
        for (uint64_t j = 0; j < n_tuple - 1; j++) {
            ixs[j] += (pawn_ix + 1);
        }
    }
    return tril_ix;
}

void pawn_tril_from_linear(uint64_t n_tuple, uint64_t tril_ix, int& pawn_ix, int* ixs) {
    int i = 0;
    while (NUMBER_OF_ORDER_TUPLES_WITH_FIRST_PAWN[i][n_tuple-1] <= tril_ix) {
        i++;
    }
    tril_ix -= NUMBER_OF_ORDER_TUPLES_WITH_FIRST_PAWN[i-1][n_tuple-1];
    pawn_ix = pawnix24_to_sqix48(i - 1);
    if (n_tuple > 1) {
        tril_from_linear(n_tuple - 1, tril_ix, ixs);
        for (uint64_t j = 0; j < n_tuple - 1; j++) {
            ixs[j] += (pawn_ix + 1);
        }
    }
}

void test_tril(uint64_t n_domain, uint64_t n_tuple) {
    uint64_t count = 0;
    int* ixs = (int*) calloc(sizeof(uint64_t), n_tuple);
    for (uint64_t tril_ix = 0; tril_ix < number_of_ordered_tuples(n_domain, n_tuple); tril_ix++) {
        tril_from_linear(n_tuple, tril_ix, ixs);
        uint64_t tril_ix_2 = tril_to_linear(n_tuple, ixs);
        assert(tril_ix_2 == tril_ix);
        count++;
    }
    std::cout << "Checked " << count << " tril indexes" << std::endl;
}

void test_tril_1(uint64_t n_domain) {
    uint64_t count = 0;
    uint64_t n_tuple = 1;
    uint64_t gt_count = number_of_ordered_tuples(n_domain, n_tuple);
    int* ixs = (int*) calloc(sizeof(uint64_t), n_tuple);
    int* ixs_2 = (int*) calloc(sizeof(uint64_t), n_tuple);
    for (uint64_t i = 0; i < n_domain; i++) {
        ixs[0] = i;
        uint64_t tril_ix = tril_to_linear(n_tuple, ixs);
        assert(0 <= tril_ix && tril_ix < gt_count);
        tril_from_linear(n_tuple, tril_ix, ixs_2);
        for (uint64_t t = 0; t < n_tuple; t++) { assert(ixs[t] == ixs_2[t]); }
        count++;
    }
    std::cout << "Checked " << count << " tril indexes" << std::endl;
    assert(count == gt_count);
}
void test_tril_2(uint64_t n_domain) {
    uint64_t count = 0;
    uint64_t n_tuple = 2;
    uint64_t gt_count = number_of_ordered_tuples(n_domain, n_tuple);
    int* ixs = (int*) calloc(sizeof(uint64_t), n_tuple);
    int* ixs_2 = (int*) calloc(sizeof(uint64_t), n_tuple);
    for (uint64_t i = 0; i < n_domain; i++) {
        for (uint64_t j = i+1; j < n_domain; j++) {
            ixs[0] = i;
            ixs[1] = j;

            uint64_t tril_ix = tril_to_linear(n_tuple, ixs);
            assert(0 <= tril_ix && tril_ix < gt_count);
            tril_from_linear(n_tuple, tril_ix, ixs_2);
            for (uint64_t t = 0; t < n_tuple; t++) { assert(ixs[t] == ixs_2[t]); }
            count++;
        }
    }
    std::cout << "Checked " << count << " tril indexes" << std::endl;
    assert(count == gt_count);
}


void test_tril_3(uint64_t n_domain) {
    uint64_t count = 0;
    uint64_t n_tuple = 3;
    uint64_t gt_count = number_of_ordered_tuples(n_domain, n_tuple);
    int* ixs = (int*) calloc(sizeof(uint64_t), n_tuple);
    int* ixs_2 = (int*) calloc(sizeof(uint64_t), n_tuple);
    for (uint64_t i = 0; i < n_domain; i++) {
        for (uint64_t j = i+1; j < n_domain; j++) {
            for (uint64_t k = j+1; k < n_domain; k++) {
                ixs[0] = i;
                ixs[1] = j;
                ixs[2] = k;

                uint64_t tril_ix = tril_to_linear(n_tuple, ixs);
                assert(0 <= tril_ix && tril_ix < gt_count);
                tril_from_linear(n_tuple, tril_ix, ixs_2);
                for (uint64_t t = 0; t < n_tuple; t++) { assert(ixs[t] == ixs_2[t]); }
                count++;
            }
        }
    }
    std::cout << "Checked " << count << " tril indexes" << std::endl;
    assert(count == gt_count);
}

void test_tril_4(uint64_t n_domain) {
    uint64_t count = 0;
    uint64_t n_tuple = 4;
    uint64_t gt_count = number_of_ordered_tuples(n_domain, n_tuple);
    int* ixs = (int*) calloc(sizeof(uint64_t), n_tuple);
    int* ixs_2 = (int*) calloc(sizeof(uint64_t), n_tuple);
    for (uint64_t i = 0; i < n_domain; i++) {
        for (uint64_t j = i+1; j < n_domain; j++) {
            for (uint64_t k = j+1; k < n_domain; k++) {
                for (uint64_t l = k+1; l < n_domain; l++) {
                    ixs[0] = i;
                    ixs[1] = j;
                    ixs[2] = k;
                    ixs[3] = l;

                    uint64_t tril_ix = tril_to_linear(n_tuple, ixs);
                    assert(0 <= tril_ix && tril_ix < gt_count);
                    tril_from_linear(n_tuple, tril_ix, ixs_2);
                    for (uint64_t t = 0; t < n_tuple; t++) { assert(ixs[t] == ixs_2[t]); }
                    count++;
                }
            }
        }
    }
    std::cout << "Checked " << count << " tril indexes" << std::endl;
    assert(count == gt_count);
}

void test_pawn_tril(uint64_t n_tuple) {
    uint64_t count = 0;
    int pawn_ix = 0;
    int* ixs = (int*) calloc(sizeof(uint64_t), n_tuple - 1);
    for (uint64_t tril_ix = 0; tril_ix < number_of_ordered_tuples_with_first_pawn(n_tuple); tril_ix++) {
        pawn_tril_from_linear(n_tuple, tril_ix, pawn_ix, ixs);
        uint64_t tril_ix_2 = pawn_tril_to_linear(n_tuple, pawn_ix, ixs);
        // std::cout << tril_ix << " vs " << tril_ix_2 << " - pawn_ix=" << pawn_ix; for (int j = 0; j < n_tuple - 1; j++) std::cout << "," << ixs[j]; std::cout << std::endl;
        assert(tril_ix_2 == tril_ix);
        count++;
    }
    std::cout << "Checked " << count << " tril indexes" << std::endl;
}

void test_pawn_tril_1() {
    uint64_t count = 0;
    uint64_t n_tuple = 1;
    uint64_t gt_count = number_of_ordered_tuples_with_first_pawn(n_tuple);
    int pawn_ix = 0;
    int* ixs = (int*) calloc(sizeof(uint64_t), n_tuple-1);
    int pawn_ix_2 = 0;
    int* ixs_2 = (int*) calloc(sizeof(uint64_t), n_tuple-1);
    for (uint64_t i = 0; i < 24; i++) {
        pawn_ix = pawnix24_to_sqix48(i);

        uint64_t tril_ix = pawn_tril_to_linear(n_tuple, pawn_ix, ixs);
        assert(0 <= tril_ix && tril_ix < gt_count);
        pawn_tril_from_linear(n_tuple, tril_ix, pawn_ix_2, ixs_2);
        // std::cout << "tril_ix=" << tril_ix << ": " << pawn_ix << " vs " << pawn_ix_2; for (int ll = 0; ll < n_tuple - 1; ll++) std::cout << ", " << ixs[ll] << " vs " << ixs_2[ll]; std::cout << std::endl;
        assert(pawn_ix == pawn_ix_2);
        for (uint64_t t = 0; t < n_tuple; t++) { assert(ixs[t] == ixs_2[t]); }
        count++;
    }
    std::cout << "Checked " << count << " tril indexes" << std::endl;
    assert(count == gt_count);
}

void test_pawn_tril_2() {
    uint64_t count = 0;
    uint64_t n_tuple = 2;
    uint64_t gt_count = number_of_ordered_tuples_with_first_pawn(n_tuple);
    int pawn_ix = 0;
    int* ixs = (int*) calloc(sizeof(uint64_t), n_tuple-1);
    int pawn_ix_2 = 0;
    int* ixs_2 = (int*) calloc(sizeof(uint64_t), n_tuple-1);
    for (uint64_t i = 0; i < 24; i++) {
        pawn_ix = pawnix24_to_sqix48(i);
        for (uint64_t j = pawn_ix+1; j < 48; j++) {
            ixs[0] = j;

            uint64_t tril_ix = pawn_tril_to_linear(n_tuple, pawn_ix, ixs);
            assert(0 <= tril_ix && tril_ix < gt_count);
            pawn_tril_from_linear(n_tuple, tril_ix, pawn_ix_2, ixs_2);
            // std::cout << "tril_ix=" << tril_ix << ": " << pawn_ix << " vs " << pawn_ix_2; for (int ll = 0; ll < n_tuple - 1; ll++) std::cout << ", " << ixs[ll] << " vs " << ixs_2[ll]; std::cout << std::endl;
            assert(pawn_ix == pawn_ix_2);
            for (uint64_t t = 0; t < n_tuple; t++) { assert(ixs[t] == ixs_2[t]); }
            count++;
        }
    }
    std::cout << "Checked " << count << " tril indexes" << std::endl;
    assert(count == gt_count);
}

void test_pawn_tril_3() {
    uint64_t count = 0;
    uint64_t n_tuple = 3;
    uint64_t gt_count = number_of_ordered_tuples_with_first_pawn(n_tuple);
    int pawn_ix = 0;
    int* ixs = (int*) calloc(sizeof(uint64_t), n_tuple-1);
    int pawn_ix_2 = 0;
    int* ixs_2 = (int*) calloc(sizeof(uint64_t), n_tuple-1);
    for (uint64_t i = 0; i < 24; i++) {
        pawn_ix = pawnix24_to_sqix48(i);
        for (uint64_t j = pawn_ix+1; j < 48; j++) {
            for (uint64_t k = j+1; k < 48; k++) {
                ixs[0] = j;
                ixs[1] = k;

                uint64_t tril_ix = pawn_tril_to_linear(n_tuple, pawn_ix, ixs);
                assert(0 <= tril_ix && tril_ix < gt_count);
                pawn_tril_from_linear(n_tuple, tril_ix, pawn_ix_2, ixs_2);
                assert(pawn_ix == pawn_ix_2);
                for (uint64_t t = 0; t < n_tuple; t++) { assert(ixs[t] == ixs_2[t]); }
                count++;
            }
        }
    }
    std::cout << "Checked " << count << " tril indexes" << std::endl;
    assert(count == gt_count);
}

void test_pawn_tril_4() {
    uint64_t count = 0;
    uint64_t n_tuple = 4;
    uint64_t gt_count = number_of_ordered_tuples_with_first_pawn(n_tuple);
    int pawn_ix = 0;
    int* ixs = (int*) calloc(sizeof(uint64_t), n_tuple-1);
    int pawn_ix_2 = 0;
    int* ixs_2 = (int*) calloc(sizeof(uint64_t), n_tuple-1);
    for (uint64_t i = 0; i < 24; i++) {
        pawn_ix = pawnix24_to_sqix48(i);
        for (uint64_t j = pawn_ix+1; j < 48; j++) {
            for (uint64_t k = j+1; k < 48; k++) {
                for (uint64_t l = k+1; l < 48; l++) {
                    ixs[0] = j;
                    ixs[1] = k;
                    ixs[2] = l;

                    uint64_t tril_ix = pawn_tril_to_linear(n_tuple, pawn_ix, ixs);
                    assert(0 <= tril_ix && tril_ix < gt_count);
                    pawn_tril_from_linear(n_tuple, tril_ix, pawn_ix_2, ixs_2);
                    assert(pawn_ix == pawn_ix_2);
                    for (uint64_t t = 0; t < n_tuple; t++) { assert(ixs[t] == ixs_2[t]); }
                    count++;
                }
            }
        }
    }
    std::cout << "Checked " << count << " tril indexes" << std::endl;
    assert(count == gt_count);
}

// uint64_t n_domain = 64;

// std::cout << "1: " << number_of_ordered_tuples(n_domain, 1) << std::endl;
// std::cout << "2: " << number_of_ordered_tuples(n_domain, 2) << std::endl;
// std::cout << "3: " << number_of_ordered_tuples(n_domain, 3) << std::endl;
// std::cout << "4: " << number_of_ordered_tuples(n_domain, 4) << std::endl;

// int ixs[4] = {4, 7, 8, 13};
// uint64_t tril_ix = tril_to_linear(4, ixs);
// std::cout << tril_ix << std::endl;
// int ixs2[4] = {0, 0, 0, 0};
// tril_from_linear(4, tril_ix, ixs2);
// for (int i = 0; i < 4; i++) { std::cout << ixs2[i] << " "; }; std::cout << std::endl;

// test_tril_1(n_domain);
// test_tril(n_domain, 1);
// test_tril_2(n_domain);
// test_tril(n_domain, 2);
// test_tril_3(n_domain);
// test_tril(n_domain, 3);
// test_tril_4(n_domain);
// test_tril(n_domain, 4);

#endif