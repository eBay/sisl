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
//  Copyright © 2016 Kadayam, Hari. All rights reserved.
#pragma once

#include <memory>
#include <array>
#include <vector>

namespace sisl {

#define VECTOR_POOL_CACHE_COUNT 100

template < typename T, size_t CacheCount = VECTOR_POOL_CACHE_COUNT >
class VectorPoolImpl {
public:
    VectorPoolImpl() {
        for (auto i = 0u; i < CacheCount; ++i) {
            m_pool[i] = new std::vector< T >();
        }
        m_last = CacheCount;
    }

    ~VectorPoolImpl() {
        for (auto i = 0u; i < m_last; ++i) {
            delete (m_pool[i]);
        }
    }

    std::vector< T >* allocate() { return (m_last == 0) ? new std::vector< T >() : m_pool[--m_last]; }
    void deallocate(std::vector< T >* v) {
        if (m_last == CacheCount) {
            delete (v);
        } else {
            v->clear();
            m_pool[m_last++] = v;
        }
    }

private:
    std::array< std::vector< T >*, CacheCount > m_pool;
    size_t m_last = 0;
};

template < typename T, size_t CacheCount = VECTOR_POOL_CACHE_COUNT >
class VectorPool {
public:
    static std::vector< T >* alloc() { return impl().allocate(); }
    static void free(std::vector< T >* v, bool no_cache = false) { no_cache ? delete (v) : impl().deallocate(v); }

private:
    static VectorPoolImpl< T, CacheCount >& impl() {
        static thread_local VectorPoolImpl< T, CacheCount > _impl;
        return _impl;
    }
};

} // namespace sisl
