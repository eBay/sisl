/*
 * hashbuf.hpp
 *
 *  Created on: 04-Apr-2017
 *      Author: hkadayam
 */
#ifndef __LIBUTILS_FDS_HASHBUF_HPP__
#define __LIBUTILS_FDS_HASHBUF_HPP__

#include "PMurHash.hpp"

namespace fds {

#ifdef IOVEC_DEFINED
// Compute the hash for list of iovectors.
static uint32_t hash32(iovec_list* iovs) {
    uint32_t h1 = 0;
    uint32_t carry = 0;

    size_t total_len = 0;
    for (auto i = 0; i < iovs->iovcnt; i++) {
        PMurHash32_Process(&h1, &carry, (const void*)iovs->iov[i].iov_base, iovs->iov[i].iov_len);
        total_len += iovs->iov[i].iov_len;
    }
    return PMurHash32_Result(h1, carry, total_len);
}
#endif

struct hash_context_t {
    uint32_t seed;
    uint32_t carry;
    size_t len;
};

static hash_context_t hash32_block_start() {
    hash_context_t ctx;
    ctx.seed = 0;
    ctx.carry = 0;
    ctx.len = 0;
    return ctx;
}

static void hash32_add_block(hash_context_t& ctx, uint8_t* blk, int32_t blk_size) {
    PMurHash32_Process(&ctx.seed, &ctx.carry, blk, blk_size);
    ctx.len += blk_size;
}

static uint32_t hash32_block_result(hash_context_t& ctx) { return PMurHash32_Result(ctx.seed, ctx.carry, ctx.len); }
} // namespace fds
#endif // __LIBUTILS_FDS_HASHMAP_HPP__
