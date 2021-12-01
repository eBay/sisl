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

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <type_traits>

#include "boost/preprocessor/arithmetic/inc.hpp"
#include "boost/preprocessor/repetition/repeat_from_to.hpp"

// NOTE: In future should use [[likely]] and [[unlikely]] but not valid syntax in if predicate
#if defined __GNUC__ || defined __llvm__
#define sisl_likely(x) __builtin_expect(!!(x), 1)
#define sisl_unlikely(x) __builtin_expect(!!(x), 0)
#else
#define sisl_likely(x) (x)
#define sisl_unlikely(x) (x)
#endif

typedef std::chrono::steady_clock Clock;

/*************** Clock/Time Related Methods/Definitions **************/
#define CURRENT_CLOCK(name) Clock::time_point name = Clock::now()

inline uint64_t get_elapsed_time_ns(const Clock::time_point& t) {
    const std::chrono::nanoseconds ns{std::chrono::duration_cast< std::chrono::nanoseconds >(Clock::now() - t)};
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

// A simple wrapper to atomic to allow them to put it in vector or other STL containers
template < typename T >
struct atomwrapper {
    typedef std::decay_t< T > value_type;
    std::atomic< value_type > m_a;

    atomwrapper(const value_type& val) : m_a{val} {}
    atomwrapper(const std::atomic< value_type >& a) : m_a{a.load()} {}
    atomwrapper(const atomwrapper& other) : m_a{other.m_a.load()} {}
    atomwrapper& operator=(const atomwrapper& other) noexcept {
        m_a.store(other.m_a.load());
        return *this;
    }
    atomwrapper& operator=(atomwrapper&&) noexcept = delete;

    template < typename... Args >
    value_type fetch_add(Args&&... args) {
        return m_a.fetch_add(std::forward< Args >(args)...);
    }

    template < typename... Args >
    value_type fetch_sub(Args&&... args) {
        return m_a.fetch_add(std::forward< Args >(args)...);
    }

    template < typename... Args >
    value_type load(Args&&... args) const {
        return m_a.load(std::forward< Args >(args)...);
    }

    template < typename... Args >
    void store(Args&&... args) {
        m_a.store(std::forward< Args >(args)...);
    }

    std::atomic< value_type >& get() { return m_a; }
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

#define r_cast reinterpret_cast
#define s_cast static_cast
#define d_cast dynamic_cast
#define dp_cast std::dynamic_pointer_cast
#define sp_cast std::static_pointer_cast

#define uintptr_cast reinterpret_cast< uint8_t* >
#define voidptr_cast reinterpret_cast< void* >
#define int_cast static_cast< int >
#define uint32_cast static_cast< uint32_t >
#define int64_cast static_cast< int64_t >
#define uint64_cast static_cast< uint64_t >

} // namespace sisl

#if 0
template < typename T >
using d_cast = dynamic_cast< T >;

template < typename T >
using s_cast = static_cast< T >;

template < typename T >
using r_cast = reinterpret_cast< T >;

using void_cast = reinterpret_cast< void* >;
using uint8p_cast = reinterpret_cast< uint8_t* >;

template < typename T >
using dp_cast = std::dynamic_pointer_cast< T >;

template < typename T >
using sp_cast = std::static_pointer_cast< T >;
#endif
