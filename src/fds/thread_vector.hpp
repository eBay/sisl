#pragma once

#include <cstdint>
#include <vector>

#include "utility/thread_buffer.hpp"

namespace sisl {

/* This data structure inserts elements into per thread buffer and provide apis to access the elements.
 * Note :- It assumes that insert and access won't go in parallel.
 */
template < typename T >
class ThreadVector {

public:
    typedef std::pair< uint32_t, std::vector< T >* > thread_buffer_iterator;
    typedef std::pair< thread_buffer_iterator, int > thread_vector_iterator;

    ThreadVector() {}
    ThreadVector(const uint64_t size) : m_thread_buffer{size} {}
    ThreadVector(const ThreadVector&) = delete;
    ThreadVector(ThreadVector&&) noexcept = delete;
    ThreadVector& operator=(const ThreadVector&) = delete;
    ThreadVector& operator=(ThreadVector&&) noexcept = delete;

    template <typename InputType,
               typename = typename std::enable_if<
                   std::is_convertible< typename std::decay< InputType >::type, T >::value >::type >
    void push_back(InputType&& ele) {
        auto thread_vector = m_thread_buffer.get();
        thread_vector->push_back(std::forward<InputType>(ele));
    }

    T* begin(thread_vector_iterator& v_it) {
        auto b_it = m_thread_buffer.begin_iterator();
        if (!m_thread_buffer.is_valid(b_it)) { return nullptr; }
        v_it = thread_vector_iterator(b_it, -1);
        return (next(v_it));
    }

    T* next(thread_vector_iterator& v_it) {
        auto b_it = v_it.first;
        int ele_indx = v_it.second;
        auto buf = m_thread_buffer.get(b_it);
        assert(buf != nullptr);

        while (++ele_indx == (int)buf->size()) {
            b_it = m_thread_buffer.next(b_it);
            if (!(m_thread_buffer.is_valid(b_it))) { return nullptr; }
            buf = b_it.second;
            ele_indx = -1;
        }
        v_it = thread_vector_iterator(b_it, ele_indx);
        return (&((*buf)[ele_indx]));
    }

    /* it reset the size of vector. It doesn't change the capacity */
    void clear() {
        auto b_it = m_thread_buffer.begin_iterator();
        while (m_thread_buffer.is_valid(b_it)) {
            auto buf = m_thread_buffer.get(b_it);
            buf->clear();
            b_it = m_thread_buffer.next(b_it);
        }
    }

    /* It changes the capacity to zero */
    void erase() {
        auto b_it = m_thread_buffer.begin_iterator();
        while (m_thread_buffer.is_valid(b_it)) {
            auto buf = m_thread_buffer.get(b_it);
            buf->erase(buf->begin(), buf->end());
            b_it = m_thread_buffer.next(b_it);
        }
    }

    uint64_t size() {
        uint64_t size = 0;
        auto b_it = m_thread_buffer.begin_iterator();
        while (m_thread_buffer.is_valid(b_it)) {
            auto buf = m_thread_buffer.get(b_it);
            size += buf->size();
            b_it = m_thread_buffer.next(b_it);
        }
        return size;
    }

private:
    ExitSafeThreadBuffer< std::vector< T > > m_thread_buffer;
};
} // namespace sisl
