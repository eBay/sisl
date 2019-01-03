//
// Created by Kadayam, Hari on 12/21/18.
//
#pragma once

#include "wisr_framework.hpp"
#include <list>

namespace sisl { namespace fds {

template< typename T >
class list_wrapper : public std::list< T > {
public:
    static void merge(list_wrapper *one, const list_wrapper *two) {
        std::list<T> *a = (std::list<T> *)one;
        std::list<T> *b = (std::list<T> *)two;
        a->insert(a->end(), b->begin(), b->end());
    }
};

template< typename T >
class wisr_list {
public:
    void push_back(T& value) {
        m_wfw.writeable()->push_back(value);
    }

    template <class... Args>
    void emplace_back(Args&&... args) {
        m_wfw.writeable()->emplace_back(std::forward<Args>(args)...);
    }

    std::unique_ptr < std::list< T > > get_copy() {
        return m_wfw.get_copy();
    }

private:
    sisl::fds::wisr_framework< list_wrapper< T > > m_wfw;
};
}}