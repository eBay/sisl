/*
 * mempool.h
 *
 *  Created on: 20-Dec-2016
 *      Author: hkadayam
 */

#ifndef CPP_PROXY_SRC_LIBUTILS_FDS_MEM_FAST_MEMPOOL_HPP_
#define CPP_PROXY_SRC_LIBUTILS_FDS_MEM_FAST_MEMPOOL_HPP_

#include <iostream>
#include <inttypes.h>
#include <atomic>
#include <assert.h>

#include "mempool.hpp"

using namespace std;

namespace fds {

struct mempool_header {
  public:
    uint32_t opaque;
    mem_id_t next_id;
} __attribute__((packed));

class top_ptr {
  private:
    uint32_t m_gen;
    mem_id_t m_memid;

  public:
    top_ptr(uint32_t gen, uint32_t pool_id, uint32_t id) {
        m_gen = gen;
        m_memid.pool_no = pool_id;
        m_memid.internal_id = id;
    }

    top_ptr() : top_ptr(0, 0, INVALID_MEM_ID) {}

    static top_ptr form(uint64_t n) {
        top_ptr m;
        memcpy(&m, &n, sizeof(uint64_t));
        return m;
    }

    uint64_t to_uint64() {
        uint64_t* p = (uint64_t*)this;
        return (*p);
    }

    mem_id_t get_mem_id() { return m_memid; }

    uint32_t get_internal_id() { return m_memid.internal_id; }

    uint32_t get_pool_number() { return m_memid.pool_no; }

    uint32_t get_gen() { return m_gen; }

    bool is_valid() { return (m_memid.internal_id != INVALID_MEM_ID); }
} __attribute__((packed));

class fast_mempool : public mempool {
  public:
    fast_mempool(uint32_t nEntries, uint32_t sizePerObject, uint32_t pool_id) {
        m_nEntries = nEntries;
        m_pool_no = pool_id;

        m_top.store(top_ptr(0, m_pool_no, INVALID_MEM_ID).to_uint64());
        m_totalFree.store(0);
        m_gen.store(0);

#if 0
	     cout << "PreAllocate a fixed size pool for " << nEntries << " size =" << sizePerObject <<endl;
#endif

        uint32_t totalSize = nEntries * (sizePerObject + sizeof(mempool_header));
        m_basePtr = new uint8_t[totalSize];
        m_objSize = sizePerObject;
        m_entrySize = m_objSize + sizeof(mempool_header);

        uint8_t* ptr = m_basePtr;
        for (uint32_t i = 0; i < nEntries; i++) {
            uint8_t* entry = (uint8_t*)(ptr + sizeof(mempool_header));

            mempool_header* hdr = (mempool_header*)ptr;
            hdr->next_id = mem_id_t::form(i);
            fast_mempool::free(entry);
            ptr += m_entrySize;
        }
    }

    virtual ~fast_mempool() { delete (m_basePtr); }

    bool owns(uint8_t* mem) {
        if ((mem < m_basePtr) || (rawptr_to_internal_id(mem) >= m_nEntries)) { return false; }
        return true;
    }

    uint8_t* alloc(size_t size, mem_id_t* pid_m = nullptr) {
        uint64_t top_value;
        uint64_t new_top_value;
        uint8_t* raw_ptr;
        mempool_header* hdr;
        top_ptr ptr;

        do {
            top_value = m_top.load();

            ptr = top_ptr::form(top_value);
            if (!ptr.is_valid()) {
                // No objects available
                return nullptr;
            }

            hdr = internal_id_to_hdr(ptr.get_internal_id());
            raw_ptr = internal_id_to_rawptr(ptr.get_internal_id());

            // Generate a new top_value with new generate and next internal_id to current top.
            uint32_t gen = m_gen.fetch_add(1);
            new_top_value = top_ptr(gen, m_pool_no, hdr->next_id.internal_id).to_uint64();
        } while (!(m_top.compare_exchange_weak(top_value, new_top_value)));

        if (pid_m != nullptr) {
            pid_m->pool_no = m_pool_no;
            pid_m->internal_id = ptr.get_internal_id();
        }
        m_totalFree.fetch_sub(1);

        cout << "Allocated raw_ptr=" << raw_ptr << " hdr=" << hdr << endl;
        return raw_ptr;
    }

    void free(uint8_t* mem) {
        assert(owns(mem));
        free(rawptr_to_internal_id(mem));
    }

    void free(mem_id_t& id) {
        assert(m_pool_no == id.pool_no);
        free(id.internal_id);
    }

    uint8_t* mem_get(mem_id_t& id) {
        assert(id.pool_no == m_pool_no);
        return internal_id_to_rawptr(id.internal_id);
    }

    //////////// Additional methods applicable only for fast_memallocator //////////
    bool owns(mempool_header* hdr) { return (owns(hdr_to_rawptr(hdr))); }

    void free(mempool_header* hdr) { this->free(to_rawptr(hdr)); }

    void free(uint32_t internal_id) {
        uint64_t top_value;
        uint64_t new_top_value;

        mempool_header* hdr = internal_id_to_hdr(internal_id);

        // Get the header from the memory.
        do {
            top_value = m_top.load();
            hdr->next_id = top_ptr::form(top_value).get_mem_id();

            uint32_t gen = m_gen.fetch_add(1);
            new_top_value = top_ptr(gen, m_pool_no, internal_id).to_uint64();
        } while (!(m_top.compare_exchange_weak(top_value, new_top_value)));

        m_totalFree.fetch_add(1);
    }

    mem_id_t to_mem_id(uint8_t* rawptr) {
        mem_id_t mem_id;
        mem_id.internal_id = rawptr_to_internal_id(rawptr);
        mem_id.pool_no = m_pool_no;

        return mem_id;
    }

    uint8_t* to_rawptr(mempool_header* hdr) { return (hdr_to_rawptr(hdr)); }

    mempool_header* to_hdr(mem_id_t id) {
        assert(id.pool_no == m_pool_no);
        return internal_id_to_hdr(id.internal_id);
    }

    mempool_header* to_hdr(uint8_t* rawptr) { return rawptr_to_hdr(rawptr); }

  private:
    uint8_t* m_basePtr;
    uint32_t m_nEntries;
    uint32_t m_pool_no;

    atomic<uint64_t> m_top;
    atomic<uint32_t> m_gen;

    uint32_t m_objSize;
    uint32_t m_entrySize;
    atomic<uint32_t> m_totalFree;

  private:
    // All Ids here means internal_id
    mempool_header* rawptr_to_hdr(uint8_t* rawptr) { return ((mempool_header*)(rawptr - sizeof(mempool_header))); }

    uint8_t* hdr_to_rawptr(mempool_header* hdr) { return ((uint8_t*)(((uint8_t*)hdr) + sizeof(mempool_header))); }

    mempool_header* internal_id_to_hdr(uint32_t internal_id) {
        return (mempool_header*)(m_basePtr + (internal_id * m_entrySize));
    }

    uint8_t* internal_id_to_rawptr(uint32_t internal_id) {
        return (m_basePtr + (internal_id * m_entrySize) + sizeof(mempool_header));
    }

    uint32_t rawptr_to_internal_id(uint8_t* mem) {
        return ((((uint8_t*)rawptr_to_hdr(mem)) - m_basePtr) / m_entrySize);
    }
};

};     // namespace fds
#endif /* CPP_PROXY_SRC_LIBUTILS_FDS_MEM_FAST_MEMPOOL_HPP_ */
