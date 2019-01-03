//
// Created by Kadayam, Hari on 12/21/18.
//
#pragma once

#include "wisr_framework.hpp"
#include <vector>

namespace sisl { namespace fds {

template< typename T >
class vector_wrapper : public std::vector< T > {
public:
    vector_wrapper() = default;
    explicit vector_wrapper(size_t initial) : std::vector<T>() {
        std::vector< T >::reserve(initial);
    }

    static void merge(vector_wrapper *one, const vector_wrapper *two) {
        std::vector<T> *a = (std::vector<T> *)one;
        std::vector<T> *b = (std::vector<T> *)two;
        a->insert(a->end(), b->begin(), b->end());
    }
};

template< typename T >
class wisr_vector {
public:
    explicit wisr_vector(size_t initial) :
            m_wfw(initial) {
    }

    void push_back(T& value) {
        m_wfw.writeable()->push_back(value);
    }

    template <class... Args>
    void emplace_back(Args&&... args) {
        m_wfw.writeable()->emplace_back(std::forward<Args>(args)...);
    }

    std::unique_ptr < std::vector< T > > get_copy() {
        return m_wfw.get_copy();
    }

private:
    sisl::fds::wisr_framework< vector_wrapper< T >, size_t > m_wfw;
};
}}