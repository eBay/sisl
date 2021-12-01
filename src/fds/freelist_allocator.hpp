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
 * under the License is distributed on  * an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <folly/ThreadLocal.h>
#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic pop
#endif

#include "metrics/metrics.hpp"
#include "utils.hpp"

namespace sisl {

/**
 * @brief FreeListAllocator is a high performing memory allocator for a fixed size object. It uses thread local to
 * free list of same blob sizes. While jemalloc and tcmalloc maintains per thread buffer, it slightly outperforms them
 * for the specific case it supports - fixed size. While others are generic size where it needs to additionally look for
 * closest match for size requested, this allocator simply pops the top of the list and returns the buffer. It is as
 * fast as it can get from memory allocation and deallocation perspective.
 */
struct free_list_header {
    free_list_header* next;
};

#if defined(FREELIST_METRICS) || !defined(NDEBUG)
class FreeListAllocatorMetrics : public sisl::MetricsGroupWrapper {
public:
    explicit FreeListAllocatorMetrics() : sisl::MetricsGroupWrapper("FreeListAllocator", "Singleton") {
        REGISTER_COUNTER(freelist_alloc_hit, "freelist: Number of allocs from cache");
        REGISTER_COUNTER(freelist_alloc_miss, "freelist: Number of allocs from system");
        REGISTER_COUNTER(freelist_dealloc_passthru, "freelist: Number of dealloc not cached because of size mismatch");
        REGISTER_COUNTER(freelist_dealloc, "freelist: Number of deallocs to system");
        REGISTER_COUNTER(freelist_alloc_size, "freelist: size of alloc", sisl::_publish_as::publish_as_gauge);
        REGISTER_COUNTER(freelist_cache_size, "freelist: cache size", sisl::_publish_as::publish_as_gauge);

        register_me_to_farm();
    }
    FreeListAllocatorMetrics(const FreeListAllocatorMetrics&) = delete;
    FreeListAllocatorMetrics(FreeListAllocatorMetrics&&) noexcept = delete;
    FreeListAllocatorMetrics& operator=(const FreeListAllocatorMetrics&) = delete;
    FreeListAllocatorMetrics& operator=(FreeListAllocatorMetrics&&) noexcept = delete;

    ~FreeListAllocatorMetrics() { deregister_me_from_farm(); }

    static FreeListAllocatorMetrics& instance() {
        static FreeListAllocatorMetrics inst;
        return inst;
    }
};

#define COUNTER_INCREMENT_IF_ENABLED(p, v) COUNTER_INCREMENT(FreeListAllocatorMetrics::instance(), p, v);
#define COUNTER_DECREMENT_IF_ENABLED(p, v) COUNTER_DECREMENT(FreeListAllocatorMetrics::instance(), p, v);
#define INIT_METRICS [[maybe_unused]] auto& metrics = FreeListAllocatorMetrics::instance();
#else
#define COUNTER_INCREMENT_IF_ENABLED(p, v) ;
#define COUNTER_DECREMENT_IF_ENABLED(p, v) ;
#define INIT_METRICS ;
#endif

template < uint16_t MaxListCount, std::size_t Size >
class FreeListAllocatorImpl {
private:
    free_list_header* m_head;
    int64_t m_list_count;

public:
    FreeListAllocatorImpl() : m_head(nullptr), m_list_count(0) {}
    FreeListAllocatorImpl(const FreeListAllocatorImpl&) = delete;
    FreeListAllocatorImpl(FreeListAllocatorImpl&&) noexcept = delete;
    FreeListAllocatorImpl& operator=(const FreeListAllocatorImpl&) = delete;
    FreeListAllocatorImpl& operator=(FreeListAllocatorImpl&&) noexcept = delete;

    ~FreeListAllocatorImpl() {
        free_list_header* hdr{m_head};
        while (hdr) {
            free_list_header* const next{hdr->next};
            std::free(static_cast< void* >(hdr));
            hdr = next;
        }
    }

    uint8_t* allocate(const uint32_t size_needed) {
        uint8_t* ptr;
        INIT_METRICS;

        if (m_head == nullptr) {
            ptr = static_cast< uint8_t* >(std::malloc(size_needed));
            COUNTER_INCREMENT_IF_ENABLED(freelist_alloc_miss, 1);
        } else {
            ptr = reinterpret_cast< uint8_t* >(m_head);
            COUNTER_INCREMENT_IF_ENABLED(freelist_alloc_hit, 1);
            m_head = m_head->next;
            --m_list_count;
        }

        return ptr;
    }

    bool deallocate(uint8_t* const mem, const uint32_t size_alloced) {
        if ((size_alloced != Size) || (m_list_count == MaxListCount)) {
            std::free(static_cast< void* >(mem));
            return true;
        }
        auto* const hdr{reinterpret_cast< free_list_header* >(mem)};
        hdr->next = m_head;
        m_head = hdr;
        ++m_list_count;

        return true;
    }
};

template < const uint16_t MaxListCount, const size_t Size >
class FreeListAllocator {
private:
    folly::ThreadLocalPtr< FreeListAllocatorImpl< MaxListCount, Size > > m_impl;

public:
    static_assert((Size >= sizeof(uint8_t*)), "Size requested should be atleast a pointer size");

    FreeListAllocator() { m_impl.reset(nullptr); }
    FreeListAllocator(const FreeListAllocator&) = delete;
    FreeListAllocator(FreeListAllocator&&) noexcept = delete;
    FreeListAllocator& operator=(const FreeListAllocator&) = delete;
    FreeListAllocator& operator=(FreeListAllocator&&) noexcept = delete;

    ~FreeListAllocator() { m_impl.reset(nullptr); }

    uint8_t* allocate(const uint32_t size_needed) {
        if (sisl_unlikely(m_impl.get() == nullptr)) { m_impl.reset(new FreeListAllocatorImpl< MaxListCount, Size >()); }
        return (m_impl->allocate(size_needed));
    }

    bool deallocate(uint8_t* const mem, const uint32_t size_alloced) {
        if (sisl_unlikely(m_impl.get() == nullptr)) {
            std::free(static_cast< void* >(mem));
            return true;
        } else {
            return m_impl->deallocate(mem, size_alloced);
        }
    }

    bool owns(uint8_t* const mem) const { return true; }
    bool is_thread_safe_allocator() const { return true; }
};
} // namespace sisl
