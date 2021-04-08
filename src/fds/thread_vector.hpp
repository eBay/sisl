#pragma once

#include <cstdint>
#include <vector>
#include <optional>

#include "wisr/wisr_framework.hpp"
#include "wisr/wisr_ds.hpp"

namespace sisl {

struct thread_vector_iterator {
    size_t next_thread{0};
    size_t next_idx_in_thread{0};
};

/*
 * This data structure inserts elements into per thread buffer and provide apis to access the elements.
 */
template < typename T >
class ThreadVector {
public:
    ThreadVector() {}
    ThreadVector(const uint64_t size) : m_wvec{size} {}
    ThreadVector(const ThreadVector&) = delete;
    ThreadVector(ThreadVector&&) noexcept = delete;
    ThreadVector& operator=(const ThreadVector&) = delete;
    ThreadVector& operator=(ThreadVector&&) noexcept = delete;
    ~ThreadVector() { clear_old_version(); }

    template < typename InputType,
               typename = typename std::enable_if<
                   std::is_convertible< typename std::decay< InputType >::type, T >::value >::type >
    void push_back(InputType&& ele) {
        m_wvec.insertable([&ele](std::vector< T >* const tvec) { tvec->push_back(std::forward< InputType >(ele)); });
    }

    thread_vector_iterator begin(bool latest) {
        if (latest) {
            const auto new_vec{m_wvec.get_unmerged_and_reset()};
            m_per_thread_vec_ptrs.insert(std::end(m_per_thread_vec_ptrs), std::cbegin(new_vec), std::cend(new_vec));
        }
        return thread_vector_iterator{};
    }

    T* next(thread_vector_iterator& it) {
        while (it.next_thread < m_per_thread_vec_ptrs.size()) {
            const auto& tvec = m_per_thread_vec_ptrs[it.next_thread];
            if (it.next_idx_in_thread < tvec->size()) {
                return &tvec->at(it.next_idx_in_thread++);
            } else {
                ++it.next_thread;
                it.next_idx_in_thread = 0;
            }
        }
        return nullptr;
    }

    void clear() {
        m_wvec.reset();      // Erase all newer version ptrs
        clear_old_version(); // Erase all older version ptrs
    }

    size_t size() {
        size_t sz{0};
        // Get the size from older version
        for (const auto& tvec : m_per_thread_vec_ptrs) {
            sz += tvec->size();
        }

        // Get the size from current running version
        m_wvec.foreach_thread_member([&sz](const sisl::vector_wrapper< T >* tvec) {
            if (tvec) { sz += tvec->size(); }
        });
        return sz;
    }

private:
    void clear_old_version() {
        for (auto& tvec : m_per_thread_vec_ptrs) {
            delete tvec;
        }
        m_per_thread_vec_ptrs.clear();
    }

private:
    sisl::wisr_framework< sisl::vector_wrapper< T >, size_t > m_wvec;
    std::vector< sisl::vector_wrapper< T >* > m_per_thread_vec_ptrs;
};

} // namespace sisl
