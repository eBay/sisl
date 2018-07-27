/*
 * ds.hpp
 *
 *  Created on: 21-Jan-2017
 *      Author: hkadayam
 */

#ifndef DS_HPP_
#define DS_HPP_

#include "mem/memallocator.hpp"

namespace fds {
static mem_allocator* glob_mallocator = nullptr;

void fds_init() {
    // TODO: Use memory allocator.
    glob_mallocator = new mem_allocator();
}

uint8_t* malloc(size_t size, mem_blk* outblk = nullptr) {
    assert(glob_mallocator != nullptr);
    return glob_mallocator->alloc(size, outblk);
}

void free(uint8_t* mem) {
    assert(glob_mallocator != nullptr);
    return glob_mallocator->free(mem);
}

void free(mem_blk& blk) {
    assert(glob_mallocator != nullptr);
    return glob_mallocator->free(blk);
}

mem_allocator* mallocator() { return glob_mallocator; }
} // namespace fds

#endif /* DS_HPP_ */
