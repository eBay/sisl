/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
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
