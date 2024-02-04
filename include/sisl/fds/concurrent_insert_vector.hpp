/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
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
 *
 *********************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

#include <sisl/utility/thread_buffer.hpp>

namespace sisl {

//
// This data structure provides a vector where concurrent threads can safely emplace or push back the data into.
// However, it does not guarantee any access or iterations happen during the insertion. It is the responsibility of the
// user to synchornoize this behavior. This data structure is useful when the user wants to insert data into a vector
// concurrently in a fast manner and then iterate over the data later. If the user wants a vector implementation which
// reads concurrently with writer, they can use sisl::ThreadVector. This data structure is provided as a replacement for
// simplistic cases where insertion and iteration never happen concurrently. As a result it provides better performance
// than even sisl::ThreadVector and better debuggability.
//
// Benchmark shows atleast 10x better performance on more than 4 threads concurrently inserting with mutex.
//
template < typename T >
class ConcurrentInsertVector {
private:
    ExitSafeThreadBuffer< std::vector< T >, size_t > tvector_;
    std::vector< std::vector< T > const* > per_thread_vec_ptrs_;

public:
    struct iterator {
        size_t next_thread{0};
        size_t next_id_in_thread{0};
        ConcurrentInsertVector const* vec{nullptr};

        iterator() = default;
        iterator(ConcurrentInsertVector const& v) : vec{&v} {}
        iterator(ConcurrentInsertVector const& v, bool end_iterator) : vec{&v} {
            if (end_iterator) { next_thread = vec->per_thread_vec_ptrs_.size(); }
        }

        void operator++() {
            ++next_id_in_thread;
            if (next_id_in_thread >= vec->per_thread_vec_ptrs_[next_thread]->size()) {
                ++next_thread;
                next_id_in_thread = 0;
            }
        }

        bool operator==(iterator const& other) const = default;
        bool operator!=(iterator const& other) const = default;

        T const& operator*() const { return vec->per_thread_vec_ptrs_[next_thread]->at(next_id_in_thread); }
        T const* operator->() const { return &(vec->per_thread_vec_ptrs_[next_thread]->at(next_id_in_thread)); }
    };

    ConcurrentInsertVector() = default;
    ConcurrentInsertVector(size_t size) : tvector_{size} {}
    ConcurrentInsertVector(const ConcurrentInsertVector&) = delete;
    ConcurrentInsertVector(ConcurrentInsertVector&&) noexcept = delete;
    ConcurrentInsertVector& operator=(const ConcurrentInsertVector&) = delete;
    ConcurrentInsertVector& operator=(ConcurrentInsertVector&&) noexcept = delete;
    ~ConcurrentInsertVector() = default;

    template < typename InputType,
               typename = typename std::enable_if<
                   std::is_convertible< typename std::decay< InputType >::type, T >::value >::type >
    void push_back(InputType&& ele) {
        tvector_->push_back(std::forward< InputType >(ele));
    }

    template < class... Args >
    void emplace_back(Args&&... args) {
        tvector_->emplace_back(std::forward< Args >(args)...);
    }

    iterator begin() {
        tvector_.access_all_threads([this](std::vector< T > const* tvec, bool, bool) {
            if (tvec && tvec->size()) { per_thread_vec_ptrs_.push_back(tvec); }
            return false;
        });
        return iterator{*this};
    }

    iterator end() { return iterator{*this, true /* end_iterator */}; }

    void foreach_entry(auto&& cb) {
        tvector_.access_all_threads([this, &cb](std::vector< T > const* tvec, bool, bool) {
            if (tvec) {
                for (auto const& e : *tvec) {
                    cb(e);
                }
            }
            return false;
        });
    }

    size_t size() const {
        size_t sz{0};
        const_cast< ExitSafeThreadBuffer< std::vector< T >, size_t >& >(tvector_).access_all_threads(
            [this, &sz](std::vector< T > const* tvec, bool, bool) {
                if (tvec) { sz += tvec->size(); }
                return false;
            });
        return sz;
    }
};

} // namespace sisl
