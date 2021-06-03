//
// Created by Kadayam, Hari on 30/09/17.
//

#ifndef LIBUTILS_ATOMIC_COUNTER_HPP
#define LIBUTILS_ATOMIC_COUNTER_HPP

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace sisl {

template < typename T, const bool ForceUnsigned = true >
class atomic_counter {
    typedef std::conditional_t< ForceUnsigned, std::make_unsigned_t< std::decay_t< T > >, std::decay_t< T > >
        value_type;
    static_assert(std::is_integral< value_type >::value, "atomic_counter needs integer");

public:
    atomic_counter() : m_count{value_type{}} {}
    atomic_counter(const value_type count) : m_count{count} {}
    atomic_counter(const atomic_counter& other) : m_count{other.m_count.load(std::memory_order_acquire)} {}
    atomic_counter(atomic_counter&& other) noexcept : m_count{std::move(other.m_count)} {}
    atomic_counter& operator=(const atomic_counter& rhs) {
        if (this != &rhs) { m_count.store(rhs.load(std::memory_order_acquire), std::memory_order_release); }
        return *this;
    }
    atomic_counter& operator=(atomic_counter&& rhs) noexcept {
        if (this != &rhs) { m_count = std::move(rhs.m_count); }
        return *this;
    }

    value_type increment(const value_type n = 1) {
        const value_type count{m_count.fetch_add(n, std::memory_order_relaxed)};
        assert((n >= 0) ? count <= std::numeric_limits< value_type >::max() - n
                        : count >= std::numeric_limits< value_type >::min() - n);
        return count + n;
    }

    bool increment_test_eq(const value_type check, const value_type n = 1) {
        const value_type count{m_count.fetch_add(n, std::memory_order_release)};
        assert((n >= 0) ? count <= std::numeric_limits< value_type >::max() - n
                        : count >= std::numeric_limits< value_type >::min() - n);
        if (count == (check - n)) {
            // Fence the memory to prevent from any release (increment) getting reordered before returning
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    bool increment_test_ge(const value_type check, const value_type n = 1) {
        const value_type count{m_count.fetch_add(n, std::memory_order_release)};
        assert((n >= 0) ? count <= std::numeric_limits< value_type >::max() - n
                        : count >= std::numeric_limits< value_type >::min() - n);
        if (count >= (check - n)) {
            // Fence the memory to prevent from any release (increment) getting reordered before returning
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    std::pair< bool, value_type > increment_test_ge_with_count(const value_type check, const value_type n = 1) {
        const value_type count{m_count.fetch_add(n, std::memory_order_release)};
        assert((n >= 0) ? count <= std::numeric_limits< value_type >::max() - n
                        : count >= std::numeric_limits< value_type >::min() - n);
        if (count >= (check - n)) {
            // Fence the memory to prevent from any release (increment) getting reordered before returning
            std::atomic_thread_fence(std::memory_order_acquire);
            return {true, count + n};
        }
        return {false, count + n};
    }

    value_type decrement(const value_type n = 1) {
        const value_type count{m_count.fetch_sub(n, std::memory_order_release)};
        assert((n >= 0) ? count >= std::numeric_limits< value_type >::min() + n
                        : count <= std::numeric_limits< value_type >::max() + n);
        return count - n;
    }

    bool decrement_testz(const value_type n = 1) {
        const value_type count{m_count.fetch_sub(n, std::memory_order_release)};
        assert((n >= 0) ? count >= std::numeric_limits< value_type >::min() + n
                        : count <= std::numeric_limits< value_type >::max() + n);
        if (count == n) {
            // Fence the memory to prevent from any release (decrement) getting reordered before returning
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    bool decrement_test_eq(const value_type check, const value_type n = 1) {
        const value_type count{m_count.fetch_sub(n, std::memory_order_release)};
        assert((n >= 0) ? count >= std::numeric_limits< value_type >::min() + n
                        : count <= std::numeric_limits< value_type >::max() + n);
        if (count == (check + n)) {
            // Fence the memory to prevent from any release (decrement) getting reordered before returning
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    bool decrement_test_le(const value_type check, const value_type n = 1) {
        const value_type count{m_count.fetch_sub(n, std::memory_order_release)};
        assert((n >= 0) ? count >= std::numeric_limits< value_type >::min() + n
                        : count <= std::numeric_limits< value_type >::max() + n);
        if (count <= (check + n)) {
            // Fence the memory to prevent from any release (decrement) getting reordered before returning
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    std::pair< bool, value_type > decrement_test_le_with_count(const value_type check, const value_type n = 1) {
        const value_type count{m_count.fetch_sub(n, std::memory_order_release)};
        assert((n >= 0) ? count >= std::numeric_limits< value_type >::min() + n
                        : count <= std::numeric_limits< value_type >::max() + n);
        if (count <= (check + n)) {
            // Fence the memory to prevent from any release (decrement) getting reordered before returning
            std::atomic_thread_fence(std::memory_order_acquire);
            return {true, count - n};
        }
        return {false, count - n};
    }

    bool test_eq(const value_type check) const {
        if (get() != check) { return false; }

        std::atomic_thread_fence(std::memory_order_acquire);
        return true;
    }

    bool test_le(const value_type check) const {
        if (get() > check) { return false; }

        std::atomic_thread_fence(std::memory_order_acquire);
        return true;
    }

    std::pair< bool, value_type > test_le_with_count(const value_type check) const {
        const value_type count{get()};
        if (count > check) { return {false, count}; }

        std::atomic_thread_fence(std::memory_order_acquire);
        return {true, count};
    }

    bool test_ge(const value_type check) const {
        if (get() < check) { return false; }

        std::atomic_thread_fence(std::memory_order_acquire);
        return true;
    }

    std::pair< bool, value_type > test_ge_with_count(const value_type check) const {
        const value_type count{get()};
        if (count < check) { return {false, count}; }

        std::atomic_thread_fence(std::memory_order_acquire);
        return {true, count};
    }

    // This is not the most optimized version of testing, since it has to
    bool testz() const {
        if (get() == 0) {
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    // This is not guaranteed to be 100% thread safe if we are using it to check for 0. Use dec_testz for decrement
    // and check or testz for just checking for 0
    value_type get() const { return m_count.load(std::memory_order_relaxed); }

    void set(const value_type n) { m_count.store(n, std::memory_order_release); }

private:
    std::atomic< value_type > m_count;
};

} // namespace sisl
#endif // LIBUTILS_ATOMIC_COUNTER_HPP
