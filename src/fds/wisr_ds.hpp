//
// Created by Kadayam, Hari on 12/21/18.
//
#pragma once

#include "wisr_framework.hpp"
#include <list>
#include <vector>
#include <deque>
#include <boost/intrusive/slist.hpp>

namespace sisl { namespace fds {

template< typename DS >
class wisr_ds_wrapper : public DS {
public:
    static void merge(wisr_ds_wrapper *one, wisr_ds_wrapper *two) {
        DS *a = (DS *)one;
        DS *b = (DS *)two;
        a->insert(a->end(), b->begin(), b->end());
    }
};

template< typename T >
class intrusive_slist_wrapper : public boost::intrusive::slist< T > {
public:
    static void merge(intrusive_slist_wrapper< T > *one, intrusive_slist_wrapper< T > *two) {
        auto *a = (boost::intrusive::slist<T> *)one;
        auto *b = (boost::intrusive::slist<T> *)two;
        a->splice_after(a->cend(), *b);
    }
};

template< typename T >
class vector_wrapper : public std::vector< T > {
public:
    vector_wrapper() = default;
    explicit vector_wrapper(size_t initial) : std::vector<T>() {
        std::vector< T >::reserve(initial);
    }

    static void merge(vector_wrapper *one, vector_wrapper *two) {
        std::vector<T> *a = (std::vector<T> *)one;
        std::vector<T> *b = (std::vector<T> *)two;
        a->insert(a->end(), b->begin(), b->end());
    }
};

#define INSERT_METHOD(method_name, ...) \
    template <class... Args> \
    void method_name(__VA_ARGS__) { \
        m_wfw.insertable()->method_name(std::forward<Args>(args)...); \
    }

template< typename DS, typename T, typename... DSArgs >
class wisr_ds {
public:
    template <class... Args1>
    explicit wisr_ds(Args1&&... args) :
            m_wfw(std::forward<Args1>(args)...) {
    }

    explicit wisr_ds(const wisr_ds& other) = default;

    void push_back(T& value) { m_wfw.insertable()->push_back(value); }

    template <class... Args>
    void emplace_back(Args&&... args) { m_wfw.insertable()->emplace_back(std::forward<Args>(args)...);}

    template <class... Args>
    void push_front(Args&&... args) { m_wfw.insertable()->push_front(std::forward<Args>(args)...);}

    template <class... Args>
    void emplace_front(Args&&... args) { m_wfw.insertable()->emplace_front(std::forward<Args>(args)...); }

    DS *accessible() { return m_wfw.accessible(); }
    std::unique_ptr < DS > get_copy_and_reset() { return m_wfw.get_copy_and_reset();}

private:
    sisl::fds::wisr_framework< DS, DSArgs... > m_wfw;
};

template <typename T>
class wisr_list : public wisr_ds< wisr_ds_wrapper< std::list<T> >, T > {
};

template <typename T>
class wisr_deque : public wisr_ds< wisr_ds_wrapper< std::deque<T> >, T > {
};

template <typename T>
class wisr_vector : public wisr_ds< vector_wrapper< T >, T, size_t > {
public:
    explicit wisr_vector(size_t sz) : wisr_ds< vector_wrapper< T >, T, size_t >(sz) {}
};

template <typename T>
class wisr_intrusive_slist : public wisr_ds< intrusive_slist_wrapper< T >, T > {
};
}}