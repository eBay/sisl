#pragma once

#include <lz4.h>

namespace sisl {

static inline int compress_bound(size_t size) { return LZ4_compressBound(size); }

static inline int compress(const char* src, char* dst, int src_size, int dst_capacity) {
    return LZ4_compress_default(src, dst, src_size, dst_capacity);
}

static inline int decompress(const char* src, char* dst, int compressed_size, int dst_capacity) {
    return LZ4_decompress_safe(src, dst, compressed_size, dst_capacity);
}

} // namespace sisl
