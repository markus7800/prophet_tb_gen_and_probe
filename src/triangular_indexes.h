#
#ifndef TRIL_IX_H_INCLUDED
#define TRIL_IX_H_INCLUDED

#include <cstdint>
#include <iostream>
#include <cassert>

uint64_t _number_of_ordered_tuples(uint64_t n_domain, uint64_t n_tuple);

extern uint64_t NUMBER_OF_ORDER_TUPLES[65][8];

void init_tril();

// = n_domain * (n_domain - 1) * (n_domain - 2) ... * (n_domain - n_tuple + 1) / factorial(n_tuple)
// = (n_domain choose n_tuple)
inline uint64_t number_of_ordered_tuples(uint64_t n_domain, uint64_t n_tuple) {
    return NUMBER_OF_ORDER_TUPLES[n_domain][n_tuple];
}

// ixs in ascending order
uint64_t tril_to_linear(uint64_t n_tuple, int* ixs);
// writes to ixs in ascending order
void tril_from_linear(uint64_t n_tuple, uint64_t tril_ix, int* ixs);


void test_tril(uint64_t n_domain, uint64_t n_tuple);

void test_tril_1(uint64_t n_domain);

void test_tril_2(uint64_t n_domain);

void test_tril_3(uint64_t n_domain);

void test_tril_4(uint64_t n_domain);

#endif