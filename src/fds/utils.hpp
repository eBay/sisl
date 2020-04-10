#pragma once

#include <chrono>
#include <atomic>
#include <iostream>
#include <array>
#include <cstdlib>
#include <memory>
#include <boost/preprocessor/repetition/repeat_from_to.hpp>
#include <boost/preprocessor/arithmetic/inc.hpp>

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

inline uint64_t get_elapsed_time_ns(Clock::time_point t) {
    std::chrono::nanoseconds ns = std::chrono::duration_cast< std::chrono::nanoseconds >(Clock::now() - t);
    return ns.count();
}

inline uint64_t get_elapsed_time_ms(Clock::time_point t) { return get_elapsed_time_ns(t) / (1000 * 1000); }
inline uint64_t get_elapsed_time_us(Clock::time_point t) { return get_elapsed_time_ns(t) / 1000; }

inline uint64_t get_elapsed_time_ns(Clock::time_point t1, Clock::time_point t2) {
    std::chrono::nanoseconds ns = std::chrono::duration_cast< std::chrono::nanoseconds >(t2 - t1);
    return ns.count();
}

inline uint64_t get_elapsed_time_us(Clock::time_point t1, Clock::time_point t2) {
    return get_elapsed_time_ns(t1, t2) / 1000;
}

inline uint64_t get_time_since_epoch_ms() {
    return std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

inline uint64_t get_elapsed_time_ms(uint64_t t) { return get_time_since_epoch_ms() - t; }

/*************** Atomic Methods Related Methods/Definitions **************/
template < typename T >
void atomic_update_max(std::atomic< T >& max_value, T const& value,
                       std::memory_order order = std::memory_order_acq_rel) noexcept {
    T prev_value = max_value.load(order);
    while (prev_value < value && !max_value.compare_exchange_weak(prev_value, value, order))
        ;
}

template < typename T >
void atomic_update_min(std::atomic< T >& min_value, T const& value,
                       std::memory_order order = std::memory_order_acq_rel) noexcept {
    T prev_value = min_value.load(order);
    while (prev_value > value && !min_value.compare_exchange_weak(prev_value, value, order))
        ;
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
    return (size_t) & (reinterpret_cast< P* >(0)->*member);
}

template < class P, class M >
inline P* container_of(const M* ptr, const M P::*member) {
    return (P*)((char*)ptr - offset_of(member));
}

template < uint32_t bits, uint32_t lshifts = 0 >
static uint64_t constexpr get_mask() {
    return uint64_t(~((uint64_t)(-1) << bits) << lshifts);
}

namespace sisl {
inline uint64_t round_up(uint64_t num_to_round, uint64_t multiple) { return (num_to_round + multiple - 1) & -multiple; }
inline uint64_t round_down(uint64_t num_to_round, uint64_t multiple) { return (num_to_round / multiple) * multiple; }

template < typename T >
struct aligned_free {
    void operator()(T* p) { std::free(p); }
};

template < class T >
using aligned_unique_ptr = std::unique_ptr< T, aligned_free< T > >;

template < class T >
aligned_unique_ptr< T > make_aligned_unique(size_t align, size_t size) {
    return aligned_unique_ptr< T >(static_cast< T* >(std::aligned_alloc(align, sisl::round_up(size, align))));
}

template < class T >
std::shared_ptr< T > make_aligned_shared(size_t align, size_t size) {
    return std::shared_ptr< T >(static_cast< T* >(std::aligned_alloc(align, sisl::round_up(size, align))),
                                aligned_free< T >());
}

struct blob {
    uint8_t* bytes;
    uint32_t size;

    blob() : blob(nullptr, 0) {}
    blob(uint8_t* _bytes, uint32_t _size) : bytes(_bytes), size(_size) {}
};

/* An extension to blob where the buffer it holds is allocated by constructor and freed during destruction. The only
 * reason why we have this instead of using vector< uint8_t > is that this supports allocating in aligned memory
 */
struct _byte_array : public blob {
    _byte_array(uint32_t sz, uint32_t alignment = 0) :
            blob((uint8_t*)((alignment == 0) ? std::malloc(sz) : std::aligned_alloc(alignment, sz)), sz) {}
    ~_byte_array() { std::free(bytes); }
};

using byte_array = std::shared_ptr< _byte_array >;

inline byte_array make_byte_array(uint32_t sz, uint32_t alignment = 0) {
    return std::make_shared< _byte_array >(sz, alignment);
}

struct byte_view {
public:
    byte_view() = default;
    byte_view(uint32_t sz, uint32_t alignment = 0) {
        m_base_buf = make_byte_array(sz, alignment);
        m_view = *m_base_buf;
    }
    byte_view(byte_array arr) : byte_view(arr, 0, arr->size) {}
    byte_view(byte_array buf, uint32_t offset, uint32_t sz) {
        m_base_buf = buf;
        m_view.bytes = buf->bytes + offset;
        m_view.size = sz;
    }

    byte_view(byte_view v, uint32_t offset, uint32_t sz) : byte_view(v.m_base_buf, offset, sz) {}

    blob get_blob() const { return m_view; }
    uint8_t* bytes() const { return m_view.bytes; }
    uint32_t size() const { return m_view.size; }
    void move_forward(uint32_t by) {
        assert(m_view.size >= by);
        m_view.bytes += by;
        m_view.size -= by;
        validate();
    }

    void set_size(uint32_t sz) { m_view.size = sz; }
    void validate() { assert((m_base_buf->bytes + m_base_buf->size) >= (m_view.bytes + m_view.size)); }

private:
    byte_array m_base_buf;
    blob m_view;
};

/********* Bitwise and math related manipulation ********/
template < int S >
struct LeftShifts {
    constexpr LeftShifts() : values() {
        for (auto i = 0; i != 256; ++i) {
            values[i] = i << S;
        }
    }

    int values[256];
};

static constexpr int64_t pow(int base, uint32_t exp) {
    int64_t val = 1;
    for (auto i = 0u; i < exp; i++) {
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
