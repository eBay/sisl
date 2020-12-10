//
// Created by Kadayam, Hari on 30/09/17.
//

#ifndef LIBUTILS_ATOMIC_COUNTER_HPP
#define LIBUTILS_ATOMIC_COUNTER_HPP

#include <atomic>
#include <cassert>
#include <cstdint>
#include <type_traits>

namespace sisl {

template < typename T >
class atomic_counter {
    // NOTE:  As written T should really be a signed integral type
    static_assert(std::is_integral< T >::value, "atomic_counter needs integer");

public:
    atomic_counter() = default;
    atomic_counter(T count) : m_count(count) {}
    atomic_counter(const atomic_counter& other) : m_count(other.m_count.load(std::memory_order_acquire)) {}
    atomic_counter(atomic_counter&& other) noexcept : m_count{std::move(other.m_count)} {}
    atomic_counter& operator=(const atomic_counter& rhs) {
        if (this != &rhs) { m_count.store(rhs.load(std::memory_order_acquire), std::memory_order_release); }
        return *this;
    }
    atomic_counter& operator=(atomic_counter&& rhs) noexcept
    {
        if (this != &rhs) { m_count = std::move(rhs.m_count); }
        return *this;
    }

    T increment(const int32_t n = 1) {
        const T count = m_count.fetch_add(n, std::memory_order_relaxed);
        return count + n;
    }

    T decrement(const int32_t n = 1) {
        const T count = m_count.fetch_sub(n, std::memory_order_release);
        assert(count > 0);
        return count - n;
    }

    bool decrement_testz(const int32_t n = 1) {
        const T count = m_count.fetch_sub(n, std::memory_order_release);
        if (count == 1) {
            // Fence the memory to prevent from any release (decrement) getting reordered before returning
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        } else {
            assert(count > 0);
        }
        return false;
    }

    bool decrement_test_eq(const T& check, const int32_t n = 1) {
        const T count = m_count.fetch_sub(n, std::memory_order_release);
        if (count == (check + 1)) {
            // Fence the memory to prevent from any release (decrement) getting reordered before returning
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    bool decrement_test_le(const T& check, const int32_t n = 1) {
        const T count = m_count.fetch_sub(n, std::memory_order_release);
        if (count <= (check + 1)) {
            // Fence the memory to prevent from any release (decrement) getting reordered before returning
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        } else {
            assert(count > 0);
        }
        return false;
    }

    bool test_le(const T& check) {
        if (m_count.load(std::memory_order_relaxed) > check) { return false; }
        return true;
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
    T get() const { return m_count.load(std::memory_order_relaxed); }

    void set(const int32_t n) { m_count.store(n, std::memory_order_release); }

    bool test_le(const uint32_t check) const {
        if (get() > check) { return false; }

        std::atomic_thread_fence(std::memory_order_acquire);
        return true;
    }

private:
    std::atomic< T > m_count;
};

} // namespace sisl
#endif // LIBUTILS_ATOMIC_COUNTER_HPP
