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

#ifndef NDEBUG
#ifndef _DEBUG
#define _DEBUG
#endif
#endif

namespace sisl {
struct blob {
protected:
    uint8_t* bytes_{nullptr};
    uint32_t size_{0};
#ifdef _DEBUG
    bool is_const_{false};
#endif

public:
    blob() = default;
    blob(uint8_t* b, uint32_t s) : bytes_{b}, size_{s} {}
    blob(uint8_t const* b, uint32_t s) : bytes_{const_cast< uint8_t* >(b)}, size_{s} {
#ifdef _DEBUG
        is_const_ = true;
#endif
    }

    uint8_t* bytes() {
        DEBUG_ASSERT_EQ(is_const_, false, "Trying to access writeable bytes with const declaration");
        return bytes_;
    }
    uint32_t size() const { return size_; }
    uint8_t const* cbytes() const { return bytes_; }

    void set_bytes(uint8_t* b) {
        DEBUG_ASSERT_EQ(is_const_, false, "Trying to access writeable bytes with const declaration");
        bytes_ = b;
    }

    void set_bytes(uint8_t const* b) {
#ifdef _DEBUG
        is_const_ = false;
#endif
        bytes_ = const_cast< uint8_t* >(b);
    }
    void set_size(uint32_t s) { size_ = s; }
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
        auto remain_size = size;

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

    void move_offset(const uint32_t size) {
        auto remain_size = size;
        const auto input_iovs_size = m_input_iovs.size();
        for (; (remain_size > 0) && (m_cur_index < input_iovs_size); ++m_cur_index, m_cur_offset = 0) {
            const auto& inp_iov = m_input_iovs[m_cur_index];
            if (remain_size < inp_iov.iov_len - m_cur_offset) {
                m_cur_offset += remain_size;
                return;
            }
            remain_size -= inp_iov.iov_len - m_cur_offset;
        }
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

template < typename T, std::size_t Alignment = 512 >
class AlignedTypeAllocator {
private:
    static_assert(Alignment >= alignof(T),
                  "Beware that types like int have minimum alignment requirements "
                  "or access will result in crashes.");

public:
    /**
     * This is only necessary because AlignedAllocator has a second template
     * argument for the alignment that will make the default
     * std::allocator_traits implementation fail during compilation.
     * @see https://stackoverflow.com/a/48062758/2191065
     *
     * Taken this from link:
     * https://stackoverflow.com/questions/60169819/modern-approach-to-making-stdvector-allocate-aligned-memory
     */
    template < class OtherT >
    struct rebind {
        using other = AlignedTypeAllocator< OtherT, Alignment >;
    };

public:
    constexpr AlignedTypeAllocator() noexcept = default;
    constexpr AlignedTypeAllocator(const AlignedTypeAllocator&) noexcept = default;

    template < typename U >
    constexpr AlignedTypeAllocator(AlignedTypeAllocator< U, Alignment > const&) noexcept {}

    T* allocate(std::size_t nelems) {
        if (nelems > std::numeric_limits< std::size_t >::max() / sizeof(T)) { throw std::bad_array_new_length(); }
        return r_cast< T* >(AlignedAllocator::allocator().aligned_alloc(Alignment, nelems * sizeof(T), buftag::common));
    }

    void deallocate(T* ptr, [[maybe_unused]] std::size_t nbytes) {
        AlignedAllocator::allocator().aligned_free(uintptr_cast(ptr), buftag::common);
    }
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

template < typename T, std::size_t Alignment = 512 >
using aligned_vector = std::vector< T, AlignedTypeAllocator< T, Alignment > >;

struct io_blob;
using io_blob_list_t = folly::small_vector< sisl::io_blob, 4 >;

struct io_blob : public blob {
protected:
    bool aligned_{false};

public:
    io_blob() = default;
    io_blob(size_t sz, uint32_t align_size = 512, buftag tag = buftag::common) {
#ifdef _DEBUG
        // Allocate buffer and initialize it with 0xEE to ensure every byte is non-null
        // This helps the upper layer test if it is affected by the original buffer value or not
        buf_alloc_and_init(sz, align_size, tag, 0xEE);
#else
        buf_alloc(sz, align_size, tag);
#endif
    }
    io_blob(uint8_t* bytes, uint32_t size, bool is_aligned) : blob(bytes, size), aligned_{is_aligned} {}
    io_blob(uint8_t const* bytes, uint32_t size, bool is_aligned) : blob(bytes, size), aligned_{is_aligned} {}
    ~io_blob() = default;

    void buf_alloc(size_t sz, uint32_t align_size = 512, buftag tag = buftag::common) {
        aligned_ = (align_size != 0);
        blob::size_ = sz;
        blob::bytes_ = aligned_ ? sisl_aligned_alloc(align_size, sz, tag) : (uint8_t*)malloc(sz);
    }

    void buf_alloc_and_init(size_t sz, uint32_t align_size = 512, buftag tag = buftag::common, uint8_t init_val = 0) {
        buf_alloc(sz, align_size, tag);
        std::memset(blob::bytes_, init_val, sz);
    }

    void buf_free(buftag tag = buftag::common) const {
        aligned_ ? sisl_aligned_free(blob::bytes_, tag) : std::free(blob::bytes_);
    }

    void buf_realloc(size_t new_size, uint32_t align_size = 512, [[maybe_unused]] buftag tag = buftag::common) {
        uint8_t* new_buf{nullptr};
        if (aligned_) {
            // aligned before, so do not need check for new align size, once aligned will be aligned on realloc also
            new_buf = sisl_aligned_realloc(blob::bytes_, align_size, new_size, blob::size_);
        } else if (align_size != 0) {
            // Not aligned before, but need aligned now
            uint8_t* const new_buf{sisl_aligned_alloc(align_size, new_size, buftag::common)};
            std::memcpy(static_cast< void* >(new_buf), static_cast< const void* >(blob::bytes_),
                        std::min(new_size, static_cast< size_t >(blob::size_)));
            std::free(blob::bytes_);
        } else {
            // don't bother about alignment, just do standard realloc
            new_buf = (uint8_t*)std::realloc(blob::bytes_, new_size);
        }

        blob::size_ = new_size;
        blob::bytes_ = new_buf;
    }

    bool is_aligned() const { return aligned_; }

    static io_blob from_string(const std::string& s) {
        return io_blob{r_cast< const uint8_t* >(s.data()), uint32_cast(s.size()), false};
    }

    static io_blob_list_t sg_list_to_ioblob_list(const sg_list& sglist) {
        io_blob_list_t ret_list;
        for (const auto& iov : sglist.iovs) {
            ret_list.emplace_back(r_cast< uint8_t* >(const_cast< void* >(iov.iov_base)), uint32_cast(iov.iov_len),
                                  false);
        }
        return ret_list;
    }
};

/* An extension to blob where the buffer it holds is allocated by constructor and freed during destruction. The only
 * reason why we have this instead of using vector< uint8_t > is that this supports allocating in aligned memory
 */
struct io_blob_safe final : public io_blob {
public:
    buftag m_tag{buftag::common};

public:
    io_blob_safe() = default;
    io_blob_safe(uint32_t sz, uint32_t alignment = 0, buftag tag = buftag::common) :
            io_blob(sz, alignment, tag), m_tag{tag} {}
    io_blob_safe(uint8_t* bytes, uint32_t size, bool is_aligned) : io_blob(bytes, size, is_aligned) {}
    io_blob_safe(uint8_t const* bytes, uint32_t size, bool is_aligned) : io_blob(bytes, size, is_aligned) {}
    ~io_blob_safe() {
        if (blob::bytes_ != nullptr) { io_blob::buf_free(m_tag); }
    }

    io_blob_safe(io_blob_safe const& other) = delete;
    io_blob_safe(io_blob_safe&& other) : io_blob(std::move(other)), m_tag(other.m_tag) {
        other.bytes_ = nullptr;
        other.size_ = 0;
    }

    io_blob_safe& operator=(io_blob_safe const& other) = delete; // Delete copy constructor
    io_blob_safe& operator=(io_blob_safe&& other) {
        if (blob::bytes_ != nullptr) { this->buf_free(m_tag); }

        *((io_blob*)this) = std::move(*((io_blob*)&other));
        m_tag = other.m_tag;

        other.bytes_ = nullptr;
        other.size_ = 0;
        return *this;
    }

    void buf_alloc(size_t sz, uint32_t align_size = 512) { io_blob::buf_alloc(sz, align_size, m_tag); }
};

using byte_array_impl = io_blob_safe;

using byte_array = std::shared_ptr< io_blob_safe >;
inline byte_array make_byte_array(uint32_t sz, uint32_t alignment = 0, buftag tag = buftag::common) {
    return std::make_shared< io_blob_safe >(sz, alignment, tag);
}

struct byte_view {
public:
    byte_view() = default;
    byte_view(uint32_t sz, uint32_t alignment = 0, buftag tag = buftag::common) {
        m_base_buf = make_byte_array(sz, alignment, tag);
        m_view.set_bytes(m_base_buf->cbytes());
        m_view.set_size(m_base_buf->size());
    }
    byte_view(byte_array buf) : byte_view(std::move(buf), 0u, buf->size()) {}
    byte_view(byte_array buf, uint32_t offset, uint32_t sz) {
        m_base_buf = std::move(buf);
        m_view.set_bytes(m_base_buf->cbytes() + offset);
        m_view.set_size(sz);
    }

    byte_view(const byte_view& v, uint32_t offset, uint32_t sz) {
        DEBUG_ASSERT_GE(v.m_view.size(), sz + offset);
        m_base_buf = v.m_base_buf;
        m_view.set_bytes(v.m_view.cbytes() + offset);
        m_view.set_size(sz);
    }
    byte_view(const sisl::io_blob& b) : byte_view(b.size(), b.is_aligned()) {}

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
    uint8_t const* bytes() const { return m_view.cbytes(); }
    uint32_t size() const { return m_view.size(); }
    void move_forward(uint32_t by) {
        DEBUG_ASSERT_GE(m_view.size(), by, "Size greater than move forward request by");
        m_view.set_bytes(m_view.cbytes() + by);
        m_view.set_size(m_view.size() - by);
        validate();
    }

    // Extract the byte_array so that caller can safely use the underlying byte_array. If the view represents the
    // entire array, it will not do any copy. If view represents only portion of array, create a copy of the byte array
    // and returns that value
    byte_array extract(uint32_t alignment = 0) const {
        if (can_do_shallow_copy()) {
            return m_base_buf;
        } else {
            auto base_buf = make_byte_array(m_view.size(), alignment, m_base_buf->m_tag);
            std::memcpy(base_buf->bytes(), m_view.cbytes(), m_view.size());
            return base_buf;
        }
    }

    bool can_do_shallow_copy() const {
        return (m_view.cbytes() == m_base_buf->cbytes()) && (m_view.size() == m_base_buf->size());
    }
    void set_size(uint32_t sz) { m_view.set_size(sz); }
    void validate() const {
        DEBUG_ASSERT_LE((void*)(m_base_buf->cbytes() + m_base_buf->size()), (void*)(m_view.cbytes() + m_view.size()),
                        "Invalid byte_view");
    }

    std::string get_string() const { return std::string(r_cast< const char* >(bytes()), uint64_cast(size())); }

private:
    byte_array m_base_buf;
    blob m_view;
};
} // namespace sisl
