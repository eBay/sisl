/*
 * test_bitset.cpp
 *
 *  Created on: 11-Feb-2017
 *      Author: hkadayam
 */
#include <iostream>
#include "libutils/fds/bitset.hpp"

int main(int argc, char** argv) {
    uint64_t n;
    sscanf(argv[1], "0x%llx", &n);
    fds::atomic_bitset bset(n);

    printf("n = %lld\n", n);

    do {
        printf("Num : ");
        bset.print();

        int reset_bit;
        bool is_avail = bset.get_next_reset_bit(0, &reset_bit);
        printf("Trailing reset bit available = %d bitnum = %d\n", is_avail, reset_bit);

        bool is_done = bset.set_next_reset_bit(0, &reset_bit);
        printf("Available to set = %d, bitnum = %d\n", is_done, reset_bit);
        if (!is_done) { break; }
    } while (true);
}
