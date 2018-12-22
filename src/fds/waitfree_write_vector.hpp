//
// Created by Kadayam, Hari on 12/21/18.
//
#pragma once

#include "waitfree_write_framework.hpp"

namespace sisl { namespace fds {

template< typename T >
class VectorWrapper : public std::vector< T > {
public:
#if 0
    VectorWrapper() {
        std::vector< T >::reserve(initial);
    }
#endif

    static void merge(VectorWrapper *one, const VectorWrapper *two) {
        std::vector<T> *a = (std::vector<T> *)one;
        std::vector<T> *b = (std::vector<T> *)two;
        a->insert(a->end(), b->begin(), b->end());
    }

#if 0
private:
    size_t m_initial = 0;
#endif
};

template< typename T >
class WaitFreeWriteVector {
public:
#if 0
    WaitFreeWriteVector(size_t initial) :
            m_wfw(initial) {
    }
#endif

    void push_back(T& value) {
        m_wfw.writeable()->push_back(value);
    }

    template <class... Args>
    void emplace_back(Args&&... args) {
        m_wfw.writeable()->emplace_back(std::forward<Args>(args)...);
    }

    std::unique_ptr < std::vector< T > > get_copy() {
        return m_wfw.readable();
    }

private:
    sisl::fds::WaitFreeWriterFramework< VectorWrapper< T > > m_wfw;
};
}}