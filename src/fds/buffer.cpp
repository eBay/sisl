#include <cstring>
#include "buffer.hpp"

namespace sisl {
uint8_t* AlignedAllocatorImpl::aligned_alloc(const size_t align, const size_t sz, const sisl::buftag tag) {
    auto* buf{static_cast< uint8_t* >(std::aligned_alloc(align, sisl::round_up(sz, align)))};
    AlignedAllocator::metrics().increment(tag, buf_size(buf));
    return buf;
}

void AlignedAllocatorImpl::aligned_free(uint8_t* const b, const sisl::buftag tag) {
    AlignedAllocator::metrics().decrement(tag, buf_size(b));
    return std::free(b);
}

uint8_t* AlignedAllocatorImpl::aligned_realloc(uint8_t* const old_buf, const size_t align, const size_t new_sz,
                                               const size_t old_sz) {
    // Glibc does not have an implementation of efficient realloc and hence we are using alloc/copy method here
    const size_t old_real_size{(old_sz == 0) ? ::malloc_usable_size(static_cast< void* >(old_buf)) : old_sz};
    if (old_real_size >= new_sz) return old_buf;

    uint8_t* const new_buf{this->aligned_alloc(align, sisl::round_up(new_sz, align), buftag::common)};
    std::memcpy(static_cast< void* >(new_buf), static_cast< const void* >(old_buf), old_real_size);

    aligned_free(old_buf, buftag::common);
    return new_buf;
}
} // namespace sisl
