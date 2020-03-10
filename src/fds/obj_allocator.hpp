/*
 * ObjectAllocator.hpp
 *
 *  Created on: 16-May-2016
 *      Author: hkadayam
 */

//  Copyright © 2016 Kadayam, Hari. All rights reserved.
#pragma once

#include <memory>
#include "freelist_allocator.hpp"

namespace sisl {

#define FREELIST_CACHE_COUNT 10000

/**
 * @brief Object Allocator is an object wrapper on top of freelist allocator. It provides convenient method to create
 * a C++ object and destruct them.
 */
template < typename T, size_t CacheCount = FREELIST_CACHE_COUNT >
class ObjectAllocator {
public:
    ObjectAllocator() {
        m_allocator = std::make_unique< sisl::FreeListAllocator< FREELIST_CACHE_COUNT, sizeof(T) > >();
    }

    template < class... Args >
    static T* make_object(Args&&... args) {
        uint8_t* mem = get_obj_allocator()->m_allocator->allocate(sizeof(T));
        T* ptr = new (mem) T(std::forward< Args >(args)...);
        return ptr;
    }

    static void deallocate(T* mem, size_t obj_size = sizeof(T)) {
        mem->~T();
        get_obj_allocator()->m_allocator->deallocate((uint8_t*)mem, obj_size);
    }

    static std::unique_ptr< ObjectAllocator< T, CacheCount > > obj_allocator;

private:
    sisl::FreeListAllocator< FREELIST_CACHE_COUNT, sizeof(T) >* get_freelist_allocator() { return m_allocator.get(); }

private:
    std::unique_ptr< sisl::FreeListAllocator< FREELIST_CACHE_COUNT, sizeof(T) > > m_allocator;

    static ObjectAllocator< T, CacheCount >* get_obj_allocator() {
        static ObjectAllocator< T, CacheCount > obj_allocator;
        return &obj_allocator;
    }
};

} // namespace sisl
