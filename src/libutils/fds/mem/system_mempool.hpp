/*
 * system_mempool.hpp
 *
 *  Created on: 27-Jan-2017
 *      Author: hkadayam
 */

#ifndef CPP_PROXY_SRC_LIBUTILS_FDS_MEM_SYSTEM_MEMPOOL_HPP_
#define CPP_PROXY_SRC_LIBUTILS_FDS_MEM_SYSTEM_MEMPOOL_HPP_

#include "mempool.hpp"
#include "libutils/fds/hash/lockfree_hashmap.hpp"

namespace fds {
class mem_handle : public fds::LFHashKey {
  public:
    mem_handle(mem_id_t id) : fds::LFHashKey((uint8_t*)&id, sizeof(mem_id_t)) {}

    // Override the compare method to just to numeric comparision.
    virtual int compare(LFHashKey& other) {
        mem_handle other_hdl = (mem_handle&)other;
        uint32_t my_handle = get_id();
        uint32_t other_handle = other_hdl.get_id();

        if (my_handle < other_handle) {
            return -1;
        } else if (my_handle > other_handle) {
            return 1;
        } else {
            return 0;
        }
    }

  private:
    uint32_t get_id() {
        uint32_t len;
        uint8_t* p = fds::LFHashKey::get_key(&len);
        assert(len == sizeof(uint32_t));
        return (*((uint32_t*)p));
    }
};

class mem_entry : public fds::LFHashValue {
  public:
    mem_entry(mem_handle hdl, uint8_t* raw_ptr) {
        m_hdl = hdl;
        m_rawptr = raw_ptr;
    }

    fds::LFHashKey* extract_key() {
        // TODO: Use dynamic_cast
        return dynamic_cast<fds::LFHashKey*>(&m_hdl);
    }

    uint8_t* get_rawptr() { return m_rawptr; }

    mem_handle m_hdl;
    uint8_t* m_rawptr;
};

#define MEM_ALLOC_BUCKETS 5000

class system_mempool : public mempool {
  public:
    system_mempool(uint32_t pool_id) {
        m_map = new fds::HashSet(MEM_ALLOC_BUCKETS);
        m_pool_id = pool_id;
        m_idcounter.store(0);
    }

    virtual ~system_mempool() {
        // TODO: Iterate over hash_table and release all memory
        delete (m_map);
    }

    /* Alloc and Free method variations */
    virtual uint8_t* alloc(size_t size, mem_id_t* pid_m = nullptr) {
        // TODO: Determine if its better to do 2 allocations instead of
        // one. In cases where application round of nearest memory block,
        // adding to its size would result in fragmented allocation.
        // Prepend the id_entry to allocated memory.
        size += sizeof(mem_entry);

        uint8_t* ptr = (uint8_t*)new uint8_t[size];
        if (ptr == nullptr) { return ptr; }

        mem_entry* e = (mem_entry*)ptr;
        e->m_rawptr = ptr + sizeof(mem_entry);

        bool done;
        do {
            mem_handle hdl(generate_id());
            mem_entry* tmp;
            e->m_hdl = hdl;

            // Create a mapping of hash handle to raw pointer.
            done = m_map->insert((fds::LFHashKey&)hdl, (fds::LFHashValue&)*e, (fds::LFHashValue**)&tmp);
            // Retry until unique id is generated.
        } while (!done);

        if (pid_m != nullptr) { *pid_m = mem_id_t::form(e->m_hdl.get_id()); }
        return (ptr + sizeof(mem_entry));
    }

    virtual void free(uint8_t* mem) {
        mem_entry* e = mem - sizeof(mem_entry);
        assert(e->get_rawptr() == mem);

        // Remove the mapping
        mem_entry* tmp;
        bool done = m_map->remove((fds::LFHashKey&)e->m_hdl, (fds::LFHashValue**)&tmp);
        assert(done == true);

        delete ((uint8_t*)mem);
    }

    /* Method to validate if the memory is owned by the pool */
    virtual bool owns(uint8_t* mem) {
        assert(0);
        return true;
    }

    /* Conversion from one to other type methods */
    virtual uint8_t* mem_get(mem_id_t& id_m) {
        mem_handle hdl(id_m);
        mem_entry* e;

        bool found = m_map->get((fds::LFHashKey&)hdl, (fds::HashNode**)&e);
        if (!found) { return nullptr; }

        assert(e->m_hdl == hdl);
        return e->m_rawptr;
    }

  private:
    mem_id_t generate_id() {
        mem_id_t id_m;
        id_m.pool_no = m_pool_id;
        id_m.internal_id = m_idcounter.fetch_add(1);

        return id_m;
    }

  private:
    fds::HashSet* m_map;
    uint32_t m_pool_id;
    std::atomic<uint32_t> m_idcounter;
};
} // namespace fds
#endif /* CPP_PROXY_SRC_LIBUTILS_FDS_MEM_SYSTEM_MEMPOOL_HPP_ */
