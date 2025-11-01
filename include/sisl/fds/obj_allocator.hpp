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

#include <cstdlib>
#include <memory>

#include "freelist_allocator.hpp"

namespace sisl {

static constexpr size_t FREELIST_CACHE_COUNT{200};

/**
 * @brief Object Allocator is an object wrapper on top of freelist allocator. It provides convenient method to create
 * a C++ object and destruct them.
 */
template < typename T, const size_t CacheCount = FREELIST_CACHE_COUNT >
class ObjectAllocator {
public:
    ObjectAllocator() {
        m_allocator = std::make_unique< sisl::FreeListAllocator< FREELIST_CACHE_COUNT, sizeof(T) > >();
    }
    ObjectAllocator(const ObjectAllocator&) = delete;
    ObjectAllocator(ObjectAllocator&&) noexcept = delete;
    ObjectAllocator& operator=(const ObjectAllocator&) = delete;
    ObjectAllocator& operator=(ObjectAllocator&&) noexcept = delete;

    template < class... Args >
    static T* make_object(Args&&... args) {
        uint8_t* const mem{get_obj_allocator()->m_allocator->allocate(sizeof(T))};
        T* const ptr{new (mem) T(std::forward< Args >(args)...)};
        return ptr;
    }

    static void deallocate(T* const mem, const size_t obj_size = sizeof(T)) {
        mem->~T();
        get_obj_allocator()->m_allocator->deallocate(reinterpret_cast< uint8_t* >(mem), obj_size);
    }

    static std::unique_ptr< ObjectAllocator< T, CacheCount > > obj_allocator;

private:
    sisl::FreeListAllocator< FREELIST_CACHE_COUNT, sizeof(T) >* get_freelist_allocator() { return m_allocator.get(); }

private:
    std::unique_ptr< sisl::FreeListAllocator< FREELIST_CACHE_COUNT, sizeof(T) > > m_allocator;

    static ObjectAllocator< T, CacheCount >* get_obj_allocator() {
        static ObjectAllocator< T, CacheCount > obj_allocator{};
        return &obj_allocator;
    }
};

} // namespace sisl
