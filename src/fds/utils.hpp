#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

#include <malloc.h>

#include "boost/preprocessor/arithmetic/inc.hpp"
#include "boost/preprocessor/repetition/repeat_from_to.hpp"

#if defined __GNUC__ || defined __llvm__
#define sisl_likely(x) __builtin_expect(!!(x), 1)
#define sisl_unlikely(x) __builtin_expect(!!(x), 0)
#else
#define sisl_likely(x) (x)
#define sisl_unlikely(x) (x)
#endif

using Clock = std::chrono::steady_clock;

/*************** Clock/Time Related Methods/Definitions **************/
#define CURRENT_CLOCK(name) Clock::time_point name = Clock::now()

inline uint64_t get_elapsed_time_ns(const Clock::time_point& t) {
    std::chrono::nanoseconds ns = std::chrono::duration_cast< std::chrono::nanoseconds >(Clock::now() - t);
    return ns.count();
}

inline uint64_t get_elapsed_time_us(const Clock::time_point& t) {
    return get_elapsed_time_ns(t) / static_cast< uint64_t >(1000);
}
inline uint64_t get_elapsed_time_ms(const Clock::time_point& t) {
    return get_elapsed_time_ns(t) / static_cast< uint64_t >(1000000);
}
inline uint64_t get_elapsed_time_sec(const Clock::time_point& t) {
    return get_elapsed_time_ns(t) / static_cast< uint64_t >(1000000000);
}

inline uint64_t get_elapsed_time_ns(const Clock::time_point& t1, const Clock::time_point& t2) {
    const std::chrono::nanoseconds ns{std::chrono::duration_cast< std::chrono::nanoseconds >(t2 - t1)};
    return ns.count();
}

inline uint64_t get_elapsed_time_us(const Clock::time_point& t1, const Clock::time_point& t2) {
    return get_elapsed_time_ns(t1, t2) / static_cast< uint64_t >(1000);
}

inline uint64_t get_time_since_epoch_ms() {
    return std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

inline uint64_t get_elapsed_time_ms(const uint64_t t) { return get_time_since_epoch_ms() - t; }

/*************** Atomic Methods Related Methods/Definitions **************/
template < typename T >
void atomic_update_max(std::atomic< T >& max_value, T const& value,
                       const std::memory_order order = std::memory_order_acq_rel) noexcept {
    T prev_value{max_value.load(order)};
    while (prev_value < value && !max_value.compare_exchange_weak(prev_value, value, order)) {}
}

template < typename T >
void atomic_update_min(std::atomic< T >& min_value, T const& value,
                       const std::memory_order order = std::memory_order_acq_rel) noexcept {
    T prev_value{min_value.load(order)};
    while (prev_value > value && !min_value.compare_exchange_weak(prev_value, value, order)) {}
}

/*************** Memory/Array Related Methods/Definitions **************/
template < unsigned... Is >
struct seq {};
template < unsigned N, unsigned... Is >
struct gen_seq : gen_seq< N - 1, N - 1, Is... > {};
template < unsigned... Is >
struct gen_seq< 0, Is... > : seq< Is... > {};

template < unsigned N1, unsigned... I1, unsigned N2, unsigned... I2 >
constexpr std::array< char const, N1 + N2 - 1 > const_concat(char const (&a1)[N1], char const (&a2)[N2], seq< I1... >,
                                                             seq< I2... >) {
    return {{a1[I1]..., a2[I2]...}};
}

template < unsigned N1, unsigned N2 >
constexpr std::array< char const, N1 + N2 - 1 > const_concat(char const (&a1)[N1], char const (&a2)[N2]) {
    return const_concat(a1, a2, gen_seq< N1 - 1 >{}, gen_seq< N2 >{});
}

#define const_concat_string(s1, s2) (&(const_concat(s1, s2)[0]))

template < class P, class M >
inline size_t offset_of(const M P::*member) {
    return reinterpret_cast< size_t >(&(reinterpret_cast< const volatile P* >(NULL)->*member));
}

template < class P, class M >
inline P* container_of(const M* ptr, const M P::*member) {
    return reinterpret_cast< P* >(reinterpret_cast< uint8_t* >(const_cast< M* >(ptr)) - offset_of(member));
}

template < const uint32_t bits, const uint32_t lshifts = 0 >
static uint64_t constexpr get_mask() {
    return ~((~static_cast< uint64_t >(0) << bits) << lshifts);
}

namespace sisl {
// NOTE: This round_up version only works for multiples a power of 2
inline uint64_t round_up(const uint64_t num_to_round, const uint64_t multiple) {
    assert((multiple > static_cast< uint64_t >(0)) && !(multiple & (multiple - 1)));
    return (num_to_round + multiple - 1) & (~(multiple - 1));
}
inline uint64_t round_down(const uint64_t num_to_round, const uint64_t multiple) {
    return (num_to_round / multiple) * multiple;
}

struct blob {
    uint8_t* bytes;
    uint32_t size;

    blob() : blob(nullptr, 0) {}
    blob(uint8_t* const _bytes, const uint32_t _size) : bytes{_bytes}, size{_size} {}
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
    virtual uint8_t* aligned_alloc(const size_t align, const size_t sz) {
        return static_cast< uint8_t* >(std::aligned_alloc(align, sisl::round_up(sz, align)));
    }

    virtual void aligned_free(uint8_t* const b) { return std::free(b); }

    virtual uint8_t* aligned_realloc(uint8_t* const old_buf, const size_t align, const size_t new_sz,
                                     const size_t old_sz = 0) {
        // Glibc does not have an implementation of efficient realloc and hence we are using alloc/copy method here
        const size_t old_real_size{(old_sz == 0) ? ::malloc_usable_size(static_cast< void* >(old_buf)) : old_sz};
        if (old_real_size >= new_sz) return old_buf;

        uint8_t* const new_buf{this->aligned_alloc(align, sisl::round_up(new_sz, align))};
        ::memcpy(static_cast< void* >(new_buf), static_cast< const void* >(old_buf), old_real_size);

        aligned_free(old_buf);
        return new_buf;
    }

    template < typename T >
    void aligned_delete(T* const p) {
        p->~T();
        aligned_free(p);
    }

    virtual size_t buf_size(uint8_t* buf) const { return ::malloc_usable_size(buf); }
};

class AlignedAllocator {
public:
    static AlignedAllocator& instance() {
        static AlignedAllocator _inst;
        return _inst;
    }
    static AlignedAllocatorImpl& allocator() { return *(instance().m_impl); }

    ~AlignedAllocator() = default;
    AlignedAllocator(const AlignedAllocator&) = delete;
    AlignedAllocator(AlignedAllocator&&) noexcept = delete;
    AlignedAllocator& operator=(const AlignedAllocator&) noexcept = delete;
    AlignedAllocator& operator=(AlignedAllocator&&) noexcept = delete;

    void set_allocator(AlignedAllocatorImpl* impl) { m_impl.reset(impl); }

private:
    AlignedAllocator() { m_impl = std::make_unique< AlignedAllocatorImpl >(); }
    std::unique_ptr< AlignedAllocatorImpl > m_impl;
};

#define sisl_aligned_alloc sisl::AlignedAllocator::allocator().aligned_alloc
#define sisl_aligned_free sisl::AlignedAllocator::allocator().aligned_free
#define sisl_aligned_realloc sisl::AlignedAllocator::allocator().aligned_realloc

template < typename T >
struct aligned_deleter {
    void operator()(T* const p) { AlignedAllocator::allocator().aligned_delete(p); }
};

template < class T >
class aligned_unique_ptr : public std::unique_ptr< T, aligned_deleter< T > > {
public:
    template < class... Args >
    static inline aligned_unique_ptr< T > make(const size_t align, Args&&... args) {
        return make_sized(align, sizeof(T), std::forward< Args >(args)...);
    }

    template < class... Args >
    static inline aligned_unique_ptr< T > make_sized(const size_t align, const size_t size, Args&&... args) {
        return aligned_unique_ptr< T >(new (sisl_aligned_alloc(align, size)) T(std::forward< Args >(args)...));
    }

    aligned_unique_ptr() = default;
    aligned_unique_ptr(T* p) : std::unique_ptr< T, aligned_deleter< T > >(p) {}
};

template < class T >
class aligned_shared_ptr : public std::shared_ptr< T > {
public:
    template < class... Args >
    static std::shared_ptr< T > make(const size_t align, Args&&... args) {
        return make_sized(align, sizeof(T), std::forward< Args >(args)...);
    }

    template < class... Args >
    static std::shared_ptr< T > make_sized(const size_t align, const size_t size, Args&&... args) {
        return std::shared_ptr< T >(new (sisl_aligned_alloc(align, size)) T(std::forward< Args >(args)...),
                                    aligned_deleter< T >());
    }

    aligned_shared_ptr(T* p) : std::shared_ptr< T >(p) {}
};

struct io_blob : public blob {
    bool aligned{false};

    io_blob() = default;
    io_blob(const size_t sz, const uint32_t align_size = 512) { buf_alloc(sz, align_size); }
    io_blob(uint8_t* const bytes, const uint32_t size, const bool is_aligned) :
            blob(bytes, size), aligned{is_aligned} {}
    ~io_blob() = default;

    void buf_alloc(const size_t sz, const uint32_t align_size = 512) {
        aligned = (align_size != 0);
        blob::size = sz;
        blob::bytes = aligned ? sisl_aligned_alloc(align_size, sz) : (uint8_t*)malloc(sz);
    }

    void buf_free() const { aligned ? sisl_aligned_free(blob::bytes) : std::free(blob::bytes); }

    void buf_realloc(const size_t new_size, const uint32_t align_size = 512) {
        uint8_t* new_buf{nullptr};
        if (aligned) {
            // aligned before, so do not need check for new align size, once aligned will be aligned on realloc also
            new_buf = sisl_aligned_realloc(blob::bytes, align_size, new_size, blob::size);
        } else if (align_size != 0) {
            // Not aligned before, but need aligned now
            uint8_t* const new_buf{sisl_aligned_alloc(align_size, new_size)};
            ::memcpy(static_cast< void* >(new_buf), static_cast< const void* >(blob::bytes),
                     std::min(new_size, static_cast< size_t >(blob::size)));
            std::free(blob::bytes);
        } else {
            // don't bother about alignment, just do standard realloc
            new_buf = (uint8_t*)std::realloc(blob::bytes, new_size);
        }

        blob::size = new_size;
        blob::bytes = new_buf;
    }
};

/* An extension to blob where the buffer it holds is allocated by constructor and freed during destruction. The only
 * reason why we have this instead of using vector< uint8_t > is that this supports allocating in aligned memory
 */
struct _io_byte_array : public io_blob {
    _io_byte_array(const uint32_t sz, const uint32_t alignment = 0) : io_blob(sz, alignment) {}
    _io_byte_array(uint8_t* const bytes, const uint32_t size, const bool is_aligned) :
            io_blob(bytes, size, is_aligned) {}
    ~_io_byte_array() { io_blob::buf_free(); }
};

using byte_array = std::shared_ptr< _io_byte_array >;
inline byte_array make_byte_array(const uint32_t sz, const uint32_t alignment = 0) {
    return std::make_shared< _io_byte_array >(sz, alignment);
}

struct byte_view {
public:
    byte_view() = default;
    byte_view(const uint32_t sz, const uint32_t alignment = 0) {
        m_base_buf = make_byte_array(sz, alignment);
        m_view = *m_base_buf;
    }
    byte_view(byte_array arr) : byte_view(std::move(arr), 0, arr->size) {}
    byte_view(byte_array buf, const uint32_t offset, const uint32_t sz) {
        m_base_buf = std::move(buf);
        m_view.bytes = buf->bytes + offset;
        m_view.size = sz;
    }

    byte_view(byte_view v, const uint32_t offset, const uint32_t sz) : byte_view(v.m_base_buf, offset, sz) {}

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
            auto base_buf = make_byte_array(m_view.size, alignment);
            memcpy(base_buf->bytes, m_view.bytes, m_view.size);
            return base_buf;
        }
    }

    bool can_do_shallow_copy() const {
        return (m_view.bytes == m_base_buf->bytes) && (m_view.size == m_base_buf->size);
    }
    void set_size(const uint32_t sz) { m_view.size = sz; }
    void validate() { assert((m_base_buf->bytes + m_base_buf->size) >= (m_view.bytes + m_view.size)); }

private:
    byte_array m_base_buf;
    blob m_view;
};

// A simple wrapper to atomic to allow them to put it in vector or other STL containers
template < typename T >
struct atomwrapper {
    std::atomic< T > m_a;

    atomwrapper(const T& val) : m_a{val} {}
    atomwrapper(const std::atomic< T >& a) : m_a{a.load()} {}
    atomwrapper(const atomwrapper& other) : m_a{other.m_a.load()} {}
    atomwrapper& operator=(const atomwrapper& other) noexcept {
        m_a.store(other.m_a.load());
        return *this;
    }
    atomwrapper& operator=(atomwrapper&&) noexcept = delete;

    template < typename... Args >
    T fetch_add(Args&&... args) {
        return m_a.fetch_add(std::forward< Args >(args)...);
    }

    template < typename... Args >
    T fetch_sub(Args&&... args) {
        return m_a.fetch_add(std::forward< Args >(args)...);
    }

    template < typename... Args >
    T load(Args&&... args) const {
        return m_a.load(std::forward< Args >(args)...);
    }

    template < typename... Args >
    void store(Args&&... args) {
        m_a.store(std::forward< Args >(args)...);
    }

    std::atomic< T >& get() { return m_a; }
};

/********* Bitwise and math related manipulation ********/
template < int S >
struct LeftShifts {
    constexpr LeftShifts() : values() {
        int i{0};
        for (auto& value : values) {
            value = (i++) << S;
        }
    }

    std::array< int, 256 > values;
};

static constexpr int64_t pow(const int64_t base, const uint32_t exp) {
    int64_t val{1};
    for (uint32_t i{0}; i < exp; ++i) {
        val *= base;
    }
    return val;
}

template < typename T >
static int spaceship_oper(const T& left, const T& right) {
    if (left == right) {
        return 0;
    } else if (left > right) {
        return -1;
    } else {
        return 1;
    }
}

#define _PLACEHOLDER_PARAM(z, n, text) , text##n
#define bind_this(method, nparams)                                                                                     \
    std::bind(&method, this BOOST_PP_REPEAT_FROM_TO(1, BOOST_PP_INC(nparams), _PLACEHOLDER_PARAM, std::placeholders::_))
} // namespace sisl
