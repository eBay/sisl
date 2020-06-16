#pragma once

#include "utility/thread_buffer.hpp"
namespace sisl {

/* This data structure inserts elements into per thread buffer and provide apis to access the elements.
 * Note :- 1. It assumes that insert and access won't go in parallel.
 *         2. It doesn't support erase.
 */
template < typename T >
class ThreadVector {

public:
    using thread_buffer_iterator = std::pair< uint32_t, std::vector< T >* >;
    using thread_vector_iterator = std::pair< thread_buffer_iterator, int >;

    ThreadVector(uint64_t size) : m_thread_buffer(size) {}
    void push_back(T& ele) {
        auto thread_vector = m_thread_buffer.get();
        thread_vector.push_back(ele);
    }

    T* begin(thread_vector_iterator& v_it) {
        auto b_it = m_thread_buffer.begin_iterator();
        if (b_it.second == nullptr) { return nullptr; }
        v_it = thread_vector_iterator(b_it, -1);
        return (next(v_it));
    }

    T* next(thread_vector_iterator& v_it) {
        auto b_it = v_it.first;
        int ele_indx = v_it.second;
        auto buf = b_it.second;
        assert(buf != nullptr);

        while (++ele_indx == buf.size()) {
            b_it = m_thread_buffer.next_iterator(b_it);
            if (b_it.second == nullptr) { return nullptr; }
            buf = b_it.second;
            ele_indx = -1;
        }
        v_it = thread_vector_iterator(b_it, ele_indx);
        return (&(buf[ele_indx]));
    }

private:
    ExitSafeThreadBuffer< std::vector< T > > m_thread_buffer;
};
} // namespace sisl
