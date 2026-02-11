#include "triangular_indexes.h"

namespace Prophet {

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

void init_tril() {
    for (uint64_t i = 0; i <= 64; i++) {
        for (uint64_t n = 0; n < 8; n++) {
            NUMBER_OF_ORDER_TUPLES[i][n] = _number_of_ordered_tuples(i, n);
        }
    }
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


void test_tril(uint64_t n_domain, uint64_t n_tuple) {
    uint64_t count = 0;
    int* ixs = (int*) calloc(n_tuple, sizeof(uint64_t));
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
    int* ixs = (int*) calloc(n_tuple, sizeof(uint64_t));
    int* ixs_2 = (int*) calloc(n_tuple, sizeof(uint64_t));
    for (uint64_t i = 0; i < n_domain; i++) {
        ixs[0] = i;
        uint64_t tril_ix = tril_to_linear(n_tuple, ixs);
        assert(tril_ix < gt_count);
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
    int* ixs = (int*) calloc(n_tuple, sizeof(uint64_t));
    int* ixs_2 = (int*) calloc(n_tuple, sizeof(uint64_t));
    for (uint64_t i = 0; i < n_domain; i++) {
        for (uint64_t j = i+1; j < n_domain; j++) {
            ixs[0] = i;
            ixs[1] = j;

            uint64_t tril_ix = tril_to_linear(n_tuple, ixs);
            assert(tril_ix < gt_count);
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
    int* ixs = (int*) calloc(n_tuple, sizeof(uint64_t));
    int* ixs_2 = (int*) calloc(n_tuple, sizeof(uint64_t));
    for (uint64_t i = 0; i < n_domain; i++) {
        for (uint64_t j = i+1; j < n_domain; j++) {
            for (uint64_t k = j+1; k < n_domain; k++) {
                ixs[0] = i;
                ixs[1] = j;
                ixs[2] = k;

                uint64_t tril_ix = tril_to_linear(n_tuple, ixs);
                assert(tril_ix < gt_count);
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
    int* ixs = (int*) calloc(n_tuple, sizeof(uint64_t));
    int* ixs_2 = (int*) calloc(n_tuple, sizeof(uint64_t));
    for (uint64_t i = 0; i < n_domain; i++) {
        for (uint64_t j = i+1; j < n_domain; j++) {
            for (uint64_t k = j+1; k < n_domain; k++) {
                for (uint64_t l = k+1; l < n_domain; l++) {
                    ixs[0] = i;
                    ixs[1] = j;
                    ixs[2] = k;
                    ixs[3] = l;

                    uint64_t tril_ix = tril_to_linear(n_tuple, ixs);
                    assert(tril_ix < gt_count);
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

} // namespace Prophet
