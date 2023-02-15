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
#pragma once
#include <memory>
#include <array>
#include <algorithm>
#include <cassert>
#include <type_traits>

#include <boost/preprocessor/stringize.hpp>
#ifdef __linux__
#include <malloc.h>
#include <sys/uio.h>
#endif
#include <folly/small_vector.h>
#include <sisl/metrics/metrics.hpp>
#include <sisl/utility/enum.hpp>
#include "utils.hpp"

namespace sisl {
struct blob {
    uint8_t* bytes;
    uint32_t size;

    blob() : blob{nullptr, 0} {}
    blob(uint8_t* const b, const uint32_t s) : bytes{b}, size{s} {}
};

using sg_iovs_t = folly::small_vector< iovec, 4 >;
struct sg_list {
    uint64_t size; // total size of data pointed by iovs;
    sg_iovs_t iovs;
};

struct sg_iterator {
    sg_iterator(const sg_iovs_t& v) : m_input_iovs{v} { assert(v.size() > 0); }

    sg_iovs_t next_iovs(uint32_t size) {
        sg_iovs_t ret_iovs;
        uint64_t remain_size = size;

        while ((remain_size > 0) && (m_cur_index < m_input_iovs.size())) {
            const auto& inp_iov = m_input_iovs[m_cur_index];
            iovec this_iov;
            this_iov.iov_base = static_cast< uint8_t* >(inp_iov.iov_base) + m_cur_offset;
            if (remain_size < inp_iov.iov_len - m_cur_offset) {
                this_iov.iov_len = remain_size;
                m_cur_offset += remain_size;
            } else {
                this_iov.iov_len = inp_iov.iov_len - m_cur_offset;
                ++m_cur_index;
                m_cur_offset = 0;
            }

            ret_iovs.push_back(this_iov);
            assert(remain_size >= this_iov.iov_len);
            remain_size -= this_iov.iov_len;
        }

        return ret_iovs;
    }

    const sg_iovs_t& m_input_iovs;
    uint64_t m_cur_offset{0};
    size_t m_cur_index{0};
};

// typedef size_t buftag_t;

// TODO: Ideally we want this to be registration, but this tag needs to be used as template
// parameter and needs to be known in compile-time. For now declaring all of the tags here
// In future will turn this into a constexpr array of sorts
VENUM(buftag, uint8_t,   // Tags
      common = 0,        // Default tag if nothing supplied
      bitset = 1,        // Default tag for bitset
      superblk = 2,      // Superblk
      metablk = 3,       // MetaBlk
      logread = 4,       // logbuf read from journal
      logwrite = 5,      // logbuf written by group commit
      compression = 6,   // Compression entries
      data_journal = 7,  // All indx_mgr data journal
      btree_journal = 8, // Journal entries for btree
      btree_node = 9,    // Data entries for btree
      sentinel = 10      // This is expected to be the last. Anything below is not registered
)

class AlignedAllocatorMetrics : public MetricsGroup {
public:
    AlignedAllocatorMetrics(const AlignedAllocatorMetrics&) = delete;
    AlignedAllocatorMetrics(AlignedAllocatorMetrics&&) noexcept = delete;
    AlignedAllocatorMetrics& operator=(const AlignedAllocatorMetrics&) = delete;
    AlignedAllocatorMetrics& operator=(AlignedAllocatorMetrics&&) noexcept = delete;

    AlignedAllocatorMetrics() : MetricsGroup("AlignedAllocation", "Singleton") {
        for (auto t{(uint8_t)buftag::common}; t < (uint8_t)buftag::sentinel; ++t) {
            const std::string name = "buftag_" + enum_name((buftag)t);
            m_tag_idx[t] = m_impl_ptr->register_counter(name, name, _publish_as::publish_as_gauge);
        }
        register_me_to_farm();
    }

    void increment(const buftag tag, const size_t size) { m_impl_ptr->counter_increment(m_tag_idx[(size_t)tag], size); }
    void decrement(const buftag tag, const size_t size) { m_impl_ptr->counter_decrement(m_tag_idx[(size_t)tag], size); }

private:
    std::array< size_t, (size_t)buftag::sentinel > m_tag_idx;
};

// NOTE:  Might consider writing this as a true allocator
class AlignedAllocatorImpl {
public:
    AlignedAllocatorImpl() = default;
    AlignedAllocatorImpl(const AlignedAllocatorImpl&) = delete;
    AlignedAllocatorImpl(AlignedAllocatorImpl&&) noexcept = delete;
    AlignedAllocatorImpl& operator=(const AlignedAllocatorImpl&) noexcept = delete;
    AlignedAllocatorImpl& operator=(AlignedAllocatorImpl&&) noexcept = delete;
    virtual ~AlignedAllocatorImpl() = default;

    virtual uint8_t* aligned_alloc(const size_t align, const size_t sz, const sisl::buftag tag);
    virtual void aligned_free(uint8_t* const b, const sisl::buftag tag);
    virtual uint8_t* aligned_realloc(uint8_t* const old_buf, const size_t align, const size_t new_sz,
                                     const size_t old_sz = 0);

    template < typename T >
    void aligned_delete(T* const p, const sisl::buftag tag) {
        if (std::is_destructible_v< std::decay_t< T > >) { p->~T(); }
        aligned_free(p, tag);
    }

    virtual uint8_t* aligned_pool_alloc(const size_t align, const size_t sz, const sisl::buftag tag) {
        return aligned_alloc(align, sz, tag);
    };
    virtual void aligned_pool_free(uint8_t* const b, const size_t, const sisl::buftag tag) { aligned_free(b, tag); };

    virtual size_t buf_size(uint8_t* buf) const {
#ifdef __linux__
        return ::malloc_usable_size(buf);
#else
        return 0;
#endif
    }
};

class AlignedAllocator {
public:
    static AlignedAllocator& instance() {
        static AlignedAllocator s_instance;
        return s_instance;
    }
    static AlignedAllocatorImpl& allocator() { return *(instance().m_impl); }
    static AlignedAllocatorMetrics& metrics() { return instance().m_metrics; }

    ~AlignedAllocator() = default;
    AlignedAllocator(const AlignedAllocator&) = delete;
    AlignedAllocator(AlignedAllocator&&) noexcept = delete;
    AlignedAllocator& operator=(const AlignedAllocator&) noexcept = delete;
    AlignedAllocator& operator=(AlignedAllocator&&) noexcept = delete;

    void set_allocator(AlignedAllocatorImpl* impl) { m_impl.reset(impl); }

private:
    AlignedAllocator() { m_impl = std::make_unique< AlignedAllocatorImpl >(); }

private:
    std::unique_ptr< AlignedAllocatorImpl > m_impl;
    AlignedAllocatorMetrics m_metrics;
};

#define sisl_aligned_alloc sisl::AlignedAllocator::allocator().aligned_alloc
#define sisl_aligned_free sisl::AlignedAllocator::allocator().aligned_free
#define sisl_aligned_realloc sisl::AlignedAllocator::allocator().aligned_realloc

template < typename T, buftag Tag >
struct aligned_deleter {
    void operator()(T* const p) { AlignedAllocator::allocator().aligned_delete(p, Tag); }
};

template < typename T, buftag Tag >
class aligned_unique_ptr : public std::unique_ptr< T, aligned_deleter< T, Tag > > {
public:
    template < class... Args >
    static inline aligned_unique_ptr< T, Tag > make(const size_t align, Args&&... args) {
        return make_sized(align, sizeof(T), std::forward< Args >(args)...);
    }

    template < class... Args >
    static inline aligned_unique_ptr< T, Tag > make_sized(const size_t align, const size_t size, Args&&... args) {
        return aligned_unique_ptr< T, Tag >(new (sisl_aligned_alloc(align, size, Tag))
                                                T(std::forward< Args >(args)...));
    }

    aligned_unique_ptr() = default;
    aligned_unique_ptr(T* p) : std::unique_ptr< T, aligned_deleter< T, Tag > >(p) {}
};

template < typename T, buftag Tag >
class aligned_shared_ptr : public std::shared_ptr< T > {
public:
    template < class... Args >
    static std::shared_ptr< T > make(const size_t align, Args&&... args) {
        return make_sized(align, sizeof(T), std::forward< Args >(args)...);
    }

    template < class... Args >
    static std::shared_ptr< T > make_sized(const size_t align, const size_t size, Args&&... args) {
        return std::shared_ptr< T >(new (sisl_aligned_alloc(align, size, Tag)) T(std::forward< Args >(args)...),
                                    aligned_deleter< T, Tag >());
    }

    aligned_shared_ptr(T* p) : std::shared_ptr< T >(p) {}
};

struct io_blob : public blob {
    bool aligned{false};

    io_blob() = default;
    io_blob(const size_t sz, const uint32_t align_size = 512, const buftag tag = buftag::common) {
        buf_alloc(sz, align_size, tag);
    }
    io_blob(uint8_t* const bytes, const uint32_t size, const bool is_aligned) :
            blob(bytes, size),
            aligned{is_aligned} {}
    ~io_blob() = default;

    void buf_alloc(const size_t sz, const uint32_t align_size = 512, const buftag tag = buftag::common) {
        aligned = (align_size != 0);
        blob::size = sz;
        blob::bytes = aligned ? sisl_aligned_alloc(align_size, sz, tag) : (uint8_t*)malloc(sz);
    }

    void buf_free(const buftag tag = buftag::common) const {
        aligned ? sisl_aligned_free(blob::bytes, tag) : std::free(blob::bytes);
    }

    void buf_realloc(const size_t new_size, const uint32_t align_size = 512,
                     [[maybe_unused]] const buftag tag = buftag::common) {
        uint8_t* new_buf{nullptr};
        if (aligned) {
            // aligned before, so do not need check for new align size, once aligned will be aligned on realloc also
            new_buf = sisl_aligned_realloc(blob::bytes, align_size, new_size, blob::size);
        } else if (align_size != 0) {
            // Not aligned before, but need aligned now
            uint8_t* const new_buf{sisl_aligned_alloc(align_size, new_size, buftag::common)};
            std::memcpy(static_cast< void* >(new_buf), static_cast< const void* >(blob::bytes),
                        std::min(new_size, static_cast< size_t >(blob::size)));
            std::free(blob::bytes);
        } else {
            // don't bother about alignment, just do standard realloc
            new_buf = (uint8_t*)std::realloc(blob::bytes, new_size);
        }

        blob::size = new_size;
        blob::bytes = new_buf;
    }

    static io_blob from_string(const std::string& s) {
        return io_blob{r_cast< uint8_t* >(const_cast< char* >(s.data())), uint32_cast(s.size()), false};
    }
};

/* An extension to blob where the buffer it holds is allocated by constructor and freed during destruction. The only
 * reason why we have this instead of using vector< uint8_t > is that this supports allocating in aligned memory
 */
struct byte_array_impl : public io_blob {
    byte_array_impl(const uint32_t sz, const uint32_t alignment = 0, const buftag tag = buftag::common) :
            io_blob(sz, alignment, tag),
            m_tag{tag} {}
    byte_array_impl(uint8_t* const bytes, const uint32_t size, const bool is_aligned) :
            io_blob(bytes, size, is_aligned) {}
    ~byte_array_impl() { io_blob::buf_free(m_tag); }

    buftag m_tag;
};

using byte_array = std::shared_ptr< byte_array_impl >;
inline byte_array make_byte_array(const uint32_t sz, const uint32_t alignment = 0, const buftag tag = buftag::common) {
    return std::make_shared< byte_array_impl >(sz, alignment, tag);
}

inline byte_array to_byte_array(const sisl::io_blob& blob) {
    return std::make_shared< byte_array_impl >(blob.bytes, blob.size, blob.aligned);
}

struct byte_view {
public:
    byte_view() = default;
    byte_view(const uint32_t sz, const uint32_t alignment = 0, const buftag tag = buftag::common) {
        m_base_buf = make_byte_array(sz, alignment, tag);
        m_view = *m_base_buf;
    }
    byte_view(byte_array buf) : byte_view(std::move(buf), 0, buf->size) {}
    byte_view(byte_array buf, const uint32_t offset, const uint32_t sz) {
        m_base_buf = std::move(buf);
        m_view.bytes = m_base_buf->bytes + offset;
        m_view.size = sz;
    }

    byte_view(const byte_view& v, const uint32_t offset, const uint32_t sz) {
        DEBUG_ASSERT_GE(v.m_view.size, sz + offset);
        m_base_buf = v.m_base_buf;
        m_view.bytes = v.m_view.bytes + offset;
        m_view.size = sz;
    }
    byte_view(const sisl::io_blob& blob) :
            byte_view(std::make_shared< byte_array_impl >(blob.bytes, blob.size, blob.aligned)) {}

    ~byte_view() = default;
    byte_view(const byte_view& other) = default;
    byte_view& operator=(const byte_view& other) = default;

    byte_view(byte_view&& other) {
        m_base_buf = std::move(other.m_base_buf);
        m_view = std::move(other.m_view);
    }

    byte_view& operator=(byte_view&& other) {
        m_base_buf = std::move(other.m_base_buf);
        m_view = std::move(other.m_view);
        return *this;
    }

    blob get_blob() const { return m_view; }
    uint8_t* bytes() const { return m_view.bytes; }
    uint32_t size() const { return m_view.size; }
    void move_forward(const uint32_t by) {
        assert(m_view.size >= by);
        m_view.bytes += by;
        m_view.size -= by;
        validate();
    }

    // Extract the byte_array so that caller can safely use the underlying byte_array. If the view represents the
    // entire array, it will not do any copy. If view represents only portion of array, create a copy of the byte array
    // and returns that value
    byte_array extract(const uint32_t alignment = 0) const {
        if (can_do_shallow_copy()) {
            return m_base_buf;
        } else {
            auto base_buf = make_byte_array(m_view.size, alignment, m_base_buf->m_tag);
            std::memcpy(base_buf->bytes, m_view.bytes, m_view.size);
            return base_buf;
        }
    }

    bool can_do_shallow_copy() const {
        return (m_view.bytes == m_base_buf->bytes) && (m_view.size == m_base_buf->size);
    }
    void set_size(const uint32_t sz) { m_view.size = sz; }
    void validate() { assert((m_base_buf->bytes + m_base_buf->size) >= (m_view.bytes + m_view.size)); }

    std::string get_string() const { return std::string(r_cast< const char* >(bytes()), uint64_cast(size())); }

private:
    byte_array m_base_buf;
    blob m_view;
};
} // namespace sisl
