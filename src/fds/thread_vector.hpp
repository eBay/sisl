#pragma once

#include "utility/thread_buffer.hpp"
namespace sisl {
typedef std::pair< int, int > thread_vector_iterator;

/* This data structure inserts elements into per thread buffer and provide apis to access the elements.
 * Note :- 1. It assumes that insert and access won't go in parallel.
 *         2. It doesn't support erase.
 */
template < typename T >
class ThreadVector {
public:
    ThreadVector(uint64_t size) : thread_buffer(size) {}
    void push_back(T ele) {
        auto thread_vector = thread_buffer.get();
        thread_vector.push_back(ele);
    }

    T begin(thread_vector_iterator& it) { return (0, 0, it); }
    T next(thread_vector_iterator& it) { return (next(it.first, ++it.second, it)); }
    bool end(thread_vector_iterator& it) {
        int buf_indx = it.first;
        int ele_indx = it.second;
        if (buf_indx != thread_buffer.get_count() || ele_indx != thread_buffer[buf_indx].size()) { return false; }
        return true;
    }

private:
    T next(int buf_indx, int ele_indx, thread_vector_iterator& it) {
        while (ele_indx == thread_buffer[buf_indx].size() && buf_indx < thread_buffer.get_count()) {
            ++buf_indx;
            ele_indx = 0;
        }
        it = std::pair< int, int >(buf_indx, ele_indx);
        return thread_buffer[buf_indx][ele_indx];
    }

private:
    ExitSafeThreadBuffer< std::vector< T > > thread_buffer;
};
} // namespace sisl
