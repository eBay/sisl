//
// Created by Kadayam, Hari on 12/21/18.
//
#pragma once

#include "wisr_framework.hpp"
#include <deque>

namespace sisl { namespace fds {

template< typename T >
class deque_wrapper : public std::deque< T > {
public:
    static void merge(deque_wrapper *one, const deque_wrapper *two) {
        std::deque<T> *a = (std::deque<T> *)one;
        std::deque<T> *b = (std::deque<T> *)two;
        a->insert(a->end(), b->begin(), b->end());
    }
};

template< typename T >
class wisr_deque {
public:
    void push_back(T& value) {
        m_wfw.writeable()->push_back(value);
    }

    void push_front(T& value) {
        m_wfw.writeable()->push_front(value);
    }

    template <class... Args>
    void emplace_back(Args&&... args) {
        m_wfw.writeable()->emplace_back(std::forward<Args>(args)...);
    }

    template <class... Args>
    void emplace_front(Args&&... args) {
        m_wfw.writeable()->emplace_front(std::forward<Args>(args)...);
    }

    std::unique_ptr < std::deque< T > > get_copy() {
        return m_wfw.get_copy();
    }

private:
    sisl::fds::wisr_framework< deque_wrapper< T > > m_wfw;
};
}}