/*
 * test_mem.cpp
 *
 *  Created on: 26-Jan-2017
 *      Author: hkadayam
 */

#include "fds/fds.hpp"

int main(int argc, char* argv[]) {
    fds::fds_init();

    for (auto i = 0; i < 10; i++) {
        uint8_t* p = fds::malloc(100);
        printf("Allocated %p\n", p);
    }
}
