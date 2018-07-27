//
// Created by Kadayam, Hari on 25/05/17.
//
#ifndef MONSTORDB_OBJ_LIFE_COUNTER_HPP_H
#define MONSTORDB_OBJ_LIFE_COUNTER_HPP_H

#include <atomic>
#include <assert.h>
#include <iostream>

namespace fds {

#ifndef NDEBUG
template <typename T> struct ObjLifeCounter {
    ObjLifeCounter() {
        m_created.fetch_add(1, std::memory_order_relaxed);
        m_alive.fetch_add(1, std::memory_order_relaxed);
    }

    /*virtual */ ~ObjLifeCounter() {
        assert(m_alive.load() > 0);
        m_alive.fetch_sub(1, std::memory_order_relaxed);
    }

    ObjLifeCounter(const ObjLifeCounter& o) noexcept { m_alive.fetch_add(1, std::memory_order_relaxed); }
    static std::atomic<int64_t> m_created;
    static std::atomic<int64_t> m_alive;
};

template <typename T> std::atomic<int64_t> ObjLifeCounter<T>::m_created(0);

template <typename T> std::atomic<int64_t> ObjLifeCounter<T>::m_alive(0);

#else

template <typename T> struct ObjLifeCounter {};
#endif // NDEBUG

} // namespace fds

#endif // MONSTORDB_OBJ_LIFE_COUNTER_HPP_H
