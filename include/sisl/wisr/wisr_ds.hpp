/************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 * Author/Developer(s): Harihara Kadayam
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 **************************************************************************/
#pragma once

#include <deque>
#include <iterator>
#include <list>
#include <vector>

#include <boost/intrusive/slist.hpp>

#include "wisr_framework.hpp"

namespace sisl {

template < typename DS >
class wisr_ds_wrapper : public DS {
public:
    wisr_ds_wrapper() = default;
    wisr_ds_wrapper(const wisr_ds_wrapper&) = delete;
    wisr_ds_wrapper(wisr_ds_wrapper&&) noexcept = delete;
    wisr_ds_wrapper& operator=(const wisr_ds_wrapper&) = delete;
    wisr_ds_wrapper& operator=(wisr_ds_wrapper&&) noexcept = delete;

    ~wisr_ds_wrapper() = default;

    static void merge(wisr_ds_wrapper* const one, wisr_ds_wrapper* const two) {
        DS* const a{static_cast< DS* >(one)};
        DS* const b{static_cast< DS* >(two)};
        a->insert(std::end(*a), std::cbegin(*b), std::cend(*b));
    }
};

template < typename T >
class intrusive_slist_wrapper : public boost::intrusive::slist< T > {
public:
    intrusive_slist_wrapper() = default;
    intrusive_slist_wrapper(const intrusive_slist_wrapper&) = delete;
    intrusive_slist_wrapper(intrusive_slist_wrapper&&) noexcept = delete;
    intrusive_slist_wrapper& operator=(const intrusive_slist_wrapper&) = delete;
    intrusive_slist_wrapper& operator=(intrusive_slist_wrapper&&) noexcept = delete;

    ~intrusive_slist_wrapper() = default;

    static void merge(intrusive_slist_wrapper< T >* const one, intrusive_slist_wrapper< T >* const two) {
        auto* const a{static_cast< boost::intrusive::slist< T >* >(one)};
        auto* const b{static_cast< boost::intrusive::slist< T >* >(two)};
        a->splice_after(std::end(*a), *b);
    }
};

template < typename T >
class vector_wrapper : public std::vector< T > {
public:
    vector_wrapper() = default;
    explicit vector_wrapper(const size_t initial) : std::vector< T >() { std::vector< T >::reserve(initial); }
    vector_wrapper(const vector_wrapper&) = delete;
    vector_wrapper(vector_wrapper&&) noexcept = delete;
    vector_wrapper& operator=(const vector_wrapper&) = delete;
    vector_wrapper& operator=(vector_wrapper&&) noexcept = delete;

    ~vector_wrapper() = default;

    static void merge(vector_wrapper* const one, const vector_wrapper* const two) {
        std::vector< T >* const a{static_cast< std::vector< T >* >(one)};
        const std::vector< T >* const b{static_cast< const std::vector< T >* >(two)};
        a->insert(std::end(*a), std::cbegin(*b), std::cend(*b));
    }
};

#define INSERT_METHOD(method_name, ...)                                                                                \
    template < class... Args >                                                                                         \
    void method_name(__VA_ARGS__) {                                                                                    \
        m_wfw.insertable_ptr()->method_name(std::forward< Args >(args)...);                                            \
    }

template < typename DS, typename T, typename... DSArgs >
class wisr_ds {
public:
    template < class... Args1 >
    explicit wisr_ds(Args1&&... args) : m_wfw(std::forward< Args1 >(args)...) {}
    wisr_ds(const wisr_ds&) = delete;
    wisr_ds(wisr_ds&&) noexcept = delete;
    wisr_ds& operator=(const wisr_ds&) = delete;
    wisr_ds& operator=(wisr_ds&&) noexcept = delete;

    ~wisr_ds() = default;

    template < typename InputType,
               typename = typename std::enable_if<
                   std::is_convertible< typename std::decay< InputType >::type, T >::value >::type >
    void push_back(InputType&& value) {
        m_wfw.insertable([&](DS* const ptr) { ptr->push_back(std::forward< InputType >(value)); });
    }

    template < class... Args >
    void emplace_back(Args&&... args) {
        m_wfw.insertable([&](DS* const ptr) { ptr->emplace_back(std::forward< Args >(args)...); });
    }

    template < class... Args >
    void push_front(Args&&... args) {
        m_wfw.insertable([&](DS* const ptr) { ptr->push_front(std::forward< Args >(args)...); });
    }

    template < class... Args >
    void emplace_front(Args&&... args) {
        m_wfw.insertable([&](DS* const ptr) { ptr->emplace_front(std::forward< Args >(args)...); });
    }

    DS* now() { return m_wfw.now(); }
    std::unique_ptr< DS > get_copy_and_reset() { return m_wfw.get_copy_and_reset(); }

private:
    sisl::wisr_framework< DS, DSArgs... > m_wfw;
};

template < typename T >
class wisr_list : public wisr_ds< wisr_ds_wrapper< std::list< T > >, T > {};

template < typename T >
class wisr_deque : public wisr_ds< wisr_ds_wrapper< std::deque< T > >, T > {};

template < typename T >
class wisr_vector : public wisr_ds< vector_wrapper< T >, T, size_t > {
public:
    explicit wisr_vector(const size_t sz) : wisr_ds< vector_wrapper< T >, T, size_t >(sz) {}
};

template < typename T >
class wisr_intrusive_slist : public wisr_ds< intrusive_slist_wrapper< T >, T > {};
} // namespace sisl
