/*
 * test_hashmap.cpp
 *
 *  Created on: 01-Mar-2017
 *      Author: hkadayam
 */

#include "cds/init.h"
#include <stdio.h>
#include "../hash/hashmap.hpp"

using namespace fds;
using namespace std;

class blk_id : public fds::LFHashKey {
  public:
    blk_id(int id) : m_id(id), fds::LFHashKey((uint8_t*)&m_id, sizeof(int)) {}

    virtual int compare(const LFHashKey& other) const {
        const blk_id& other_bid = static_cast<const blk_id&>(other);
        return (m_id - other_bid.m_id);
    }

    virtual LFHashKey& operator=(LFHashKey& other) {
        const blk_id& other_bid = static_cast<const blk_id&>(other);
        m_id = other_bid.m_id;
        return *this;
    }

    virtual blk_id& operator=(blk_id& other) {
        m_id = other.m_id;
        return *this;
    }

    virtual size_t get_hash_code() const { return std::hash<int>()(m_id); }

  private:
    int m_id;
};

class blk_entry : public fds::LFHashValue {
  public:
    blk_entry() : m_blk_id(-1) {}

    blk_entry(int id, char* contents) : m_blk_id(id) { strcpy(m_blk_contents, contents); }

    virtual ~blk_entry() {}

    virtual void set_key(LFHashKey& k) {
        blk_id& bid = static_cast<blk_id&>(k);
        m_blk_id = bid;
    }

    virtual LFHashValue& operator=(LFHashValue& other) {
        blk_entry& other_be = static_cast<blk_entry&>(other);
        m_blk_id = other_be.m_blk_id;
        strcpy(m_blk_contents, other_be.m_blk_contents);

        return *this;
    }

    virtual const LFHashKey* extract_key() const { return (const LFHashKey*)&m_blk_id; }

    char* get_contents() { return m_blk_contents; }

  private:
    blk_id m_blk_id;
    char m_blk_contents[32];
};

void insert_thread(fds::LFHashMap<fds::fixed_type_hash_set>* map, int start, int count) {
    cds::threading::Manager::attachThread();

    for (auto i = start; i < start + count; i++) {
        char contents[32];
        sprintf(contents, "Contents for Blk %d\n", i);

        blk_entry* be = new blk_entry(i, contents);
        blk_id bid(i);
        blk_entry dummy;

        // TODO: Time this with chrono
        bool ret = map->insert(bid, *be, &dummy);
        assert(ret == true);

        printf("Inserted id=%d\n", i);
    }

    cds::threading::Manager::detachThread();
}

void read_thread(fds::LFHashMap<fds::fixed_type_hash_set>* map, int start, int count) {
    cds::threading::Manager::attachThread();

    for (auto i = start; i < start + count; i++) {
        char contents[32];
        // sprintf(contents, "Contents for Blk %d\n", i);

        // blk_entry *be = new blk_entry(i, contents);
        blk_entry be;
        blk_id bid(i);

        // TODO: Time this with chrono
        bool ret = map->get(bid, &be);
        assert(ret == true);

        printf("Get id=%d, contents=%s", i, be.get_contents());
        fflush(stdout);
    }

    cds::threading::Manager::detachThread();
}

int main(int argc, char** argv) {
    std::thread* thrs[8];

    // Initialize libcds
    cds::Initialize();

    {
        // Initialize Hazard Pointer singleton
        cds::gc::DHP hpG;

        // If main thread uses lock-free containers
        // the main thread should be attached to libcds infrastructure
        cds::threading::Manager::attachThread();
        fds::LFHashMap<fds::fixed_type_hash_set> map(8000, 10);

        int count = 1000;
        int nthrs = 8;
        for (auto i = 0; i < nthrs; i++) {
            thrs[i] = new std::thread(insert_thread, &map, i * count, count);
        }

        for (auto i = 0; i < nthrs; i++) {
            thrs[i]->join();
        }

        for (auto i = 0; i < nthrs; i++) {
            thrs[i] = new std::thread(read_thread, &map, i * count, count);
        }

        for (auto i = 0; i < nthrs; i++) {
            thrs[i]->join();
        }
    }

    // Terminate libcds
    cds::Terminate();
}
