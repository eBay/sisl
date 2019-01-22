//
// Created by Kadayam, Hari on 25/05/17.
//
#pragma once

#include <atomic>
#include <cassert>
#include <typeinfo>
#include <iostream>
#include <unordered_map>
#include <string>
#include <cxxabi.h>

namespace sisl {

#ifdef _PRERELEASE
using pair_of_atomic_ptrs = std::pair<std::atomic<int64_t>*, std::atomic<int64_t>*>;

class ObjCounterRegistry {
private:
    std::unordered_map<std::string, pair_of_atomic_ptrs> m_tracker_map;

public:
    static ObjCounterRegistry& inst() {
        static ObjCounterRegistry instance;
        return instance;
    }

    static decltype(m_tracker_map)& tracker() { return inst().m_tracker_map; }

    static void register_obj(const char* name, pair_of_atomic_ptrs ptrs) { tracker()[std::string(name)] = ptrs; }

    static void foreach(const std::function< void(const std::string&, int64_t, int64_t) > &closure) {
        for(auto& e : ObjCounterRegistry::tracker()) {
            closure(e.first, e.second.first->load(std::memory_order_acquire), e.second.second->load(std::memory_order_acquire));
        }
    }
};

template <typename T> struct ObjTypeWrapper {
    ObjTypeWrapper(std::atomic<int64_t>* pc, std::atomic<int64_t>* pa) {
        int status;
        char* realname = abi::__cxa_demangle(typeid(T).name(), 0, 0, &status);
        ObjCounterRegistry::register_obj(realname, std::make_pair(pc, pa));
        free(realname);
    }
    int m_dummy; // Dummy value initialized to trigger the registratrion
};

template <typename T> struct ObjLifeCounter {
    ObjLifeCounter() {
        m_created.fetch_add(1, std::memory_order_relaxed);
        m_alive.fetch_add(1, std::memory_order_relaxed);
        m_type.m_dummy = 0; // To keep m_type initialized during compile time
    }

    /*virtual */ ~ObjLifeCounter() {
        assert(m_alive.load() > 0);
        m_alive.fetch_sub(1, std::memory_order_relaxed);
    }

    ObjLifeCounter(const ObjLifeCounter& o) noexcept {
        m_alive.fetch_add(1, std::memory_order_relaxed);
    }
    static std::atomic<int64_t> m_created;
    static std::atomic<int64_t> m_alive;
    static ObjTypeWrapper<T> m_type;
};

template <typename T> std::atomic<int64_t> ObjLifeCounter<T>::m_created(0);

template <typename T> std::atomic<int64_t> ObjLifeCounter<T>::m_alive(0);

template <typename T>
ObjTypeWrapper<T> ObjLifeCounter<T>::m_type(&ObjLifeCounter<T>::m_created, &ObjLifeCounter<T>::m_alive);

#else

template <typename DS> struct ObjLifeCounter {};
class ObjCounterRegistry {
    static ObjCounterRegistry& inst() {
        static ObjCounterRegistry instance;
        return instance;
    }

    static void register_obj(const char* name, pair_of_atomic_ptrs ptrs) {}

    static void foreach(const std::function< void(const std::string&, int64_t, int64_t) > &closure) {
    }
};
#endif // _PRERELEASE

}// namespace sisl

