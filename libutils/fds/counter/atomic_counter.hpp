/*
 * atomic_counter.hpp
 *
 *  Created on: 05-May-2017
 *      Author: hkadayam
 */

#ifndef SRC_LIBUTILS_FDS_COUNTER_ATOMIC_COUNTER_HPP_
#define SRC_LIBUTILS_FDS_COUNTER_ATOMIC_COUNTER_HPP_

#include <assert.h>
namespace fds {
template <typename T>
class atomic_counter
{
    static_assert(std::is_integral<T>::value, "atomic_counter needs integer");

public:
    atomic_counter() {
        m_count = {};
    }

    atomic_counter(T count) :
        m_count(count) {}

    T increment(int32_t n=1) {
        m_count.fetch_add(n, std::memory_order_relaxed);
        return m_count + n;
    }

    T decrement(int32_t n = 1) {
        T count = m_count.fetch_sub(n, std::memory_order_release);
        assert(count > 0);
        return count - n;
    }

    bool decrement_testz(int32_t n = 1) {
        if (m_count.fetch_sub(n, std::memory_order_release) == (T)n) {
            // Fence the memory to prevent from any release (decrement) getting reordered
            // before returning
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    // This is not the most optimized version of testing, since it has to
    bool testz() {
        if (get() == 0) {
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    // This is not guaranteed to be 100% thread safe if we are using it
    // to check for 0. Use dec_testz for decrement and check or testz for
    // just checking for 0
    T get() const {
        return m_count.load(std::memory_order_relaxed);
    }

    T get_safe() const {
        return m_count.load(std::memory_order_acquire);
    }

    void set(int32_t n) {
        m_count.store(n, std::memory_order_release);
    }
private:
    std::atomic<T> m_count;
};

} // namespace fds
#endif /* SRC_LIBUTILS_FDS_COUNTER_ATOMIC_COUNTER_HPP_ */
