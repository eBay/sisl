#pragma once
#include <snappy-c.h>
// #include <lz4.h>

namespace sisl {
#if 0
static inline int compress_bound(size_t size) { return LZ4_compressBound(size); }

static inline int compress(const char* src, char* dst, int src_size, int dst_capacity) {
    return LZ4_compress_default(src, dst, src_size, dst_capacity);
}

static inline int decompress(const char* src, char* dst, int compressed_size, int dst_capacity) {
    return LZ4_decompress_safe(src, dst, compressed_size, dst_capacity);
}
#endif

class Compress {
public:
    static size_t max_compress_len(size_t size) { return snappy_max_compressed_length(size); }

    static int compress(const char* src, char* dst, size_t src_size, size_t* dst_capacity) {
        auto ret = snappy_compress(src, src_size, dst, dst_capacity);
        if (ret == SNAPPY_OK) {
            return 0;
        } else {
            return ret;
        }
    }

    static int decompress(const char* src, char* dst, size_t compressed_size, size_t* dst_capacity) {
        auto ret = snappy_uncompress(src, compressed_size, dst, dst_capacity);
        if (ret == SNAPPY_OK) {
            return 0;
        } else {
            return ret;
        }
    }

private:
};

} // namespace sisl
