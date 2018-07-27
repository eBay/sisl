/*
 * test_list.cpp
 *
 *  Created on: 10-Feb-2017
 *      Author: hkadayam
 */

#include "cds/init.h"
#include <iostream>
#include <sstream>
#include <assert.h>
#include <thread>
#include "../list/listset.hpp"

using namespace std;

struct MyTxn {
    MyTxn(uint64_t txn_id) {
        m_txn_id = txn_id;
        std::stringstream ss;
        ss << "Txn " << m_txn_id;
        m_txn_name = ss.str();
    }

    void print() { std::cout << m_txn_name << std::endl; }

    uint64_t m_txn_id;
    std::string m_txn_name;
};

struct cursor : public fds::SkipListNode {
  public:
    cursor(uint64_t id) {
        m_cursor_id = id;
        m_txn = std::make_shared<MyTxn>(id);
        m_removed = false;
    }

    virtual int compare(const SkipListNode& ot) const {
        const cursor& other = static_cast<const cursor&>(ot);

        if (m_cursor_id < other.m_cursor_id) {
            return -1;
        } else if (m_cursor_id > other.m_cursor_id) {
            return 1;
        } else {
            return 0;
        }
    }

    virtual SkipListNode& operator=(const SkipListNode& ot) {
        const cursor& other = static_cast<const cursor&>(ot);

        m_cursor_id = other.m_cursor_id;
        m_txn = other.m_txn;
        m_removed = other.m_removed;

        return *this;
    }

    uint64_t m_cursor_id;
    std::shared_ptr<MyTxn> m_txn;
    bool m_removed;
};

cursor** glob_cursors;

void preload_thread(fds::SkipListSet* set, int start, int count) {
    printf("Preload thread start = %d, count = %d\n", start, count);
    cds::threading::Manager::attachThread();
    for (auto i = 0u; i < count; i++) {
        glob_cursors[start + i] = new cursor(start + i);
        // cout << "Preloading " << glob_cursors[start+i]->m_cursor_id << endl;
        printf("Preloading %llu\n", glob_cursors[start + i]->m_cursor_id);
        bool inserted = set->insert(*glob_cursors[start + i]);
        assert(inserted == true);
    }
    cds::threading::Manager::detachThread();
}

void insert_thread(fds::SkipListSet* set, int start, int count) {
    cds::threading::Manager::attachThread();

    for (auto i = 0u; i < count; i++) {
        glob_cursors[start + i] = new cursor(i);
        bool inserted = set->insert(*glob_cursors[start + i]);
        assert(inserted == true);
    }
    cds::threading::Manager::detachThread();
}

void read_thread(fds::SkipListSet* set, int start, int count) {
    cds::threading::Manager::attachThread();

    for (auto i = 0u; i < count; i++) {
        bool found = set->get(glob_cursors[start + i]);
        assert((found == true) || (glob_cursors[start + i]->m_removed));

        printf("Read %s\n", glob_cursors[start + i]->m_txn->m_txn_name.c_str());
        // cout << "Read " << glob_cursors[start+i]-> << endl;
    }
    cds::threading::Manager::detachThread();
}

int main(int argc, char** argv) {
    std::thread* thrs[8];
    glob_cursors = new cursor*[1000];

    // Initialize libcds
    cds::Initialize();

    // Initialize Hazard Pointer singleton
    cds::gc::DHP hpG;

    // If main thread uses lock-free containers
    // the main thread should be attached to libcds infrastructure
    cds::threading::Manager::attachThread();
    fds::SkipListSet set;

    int count = 100;
    int nthrs = 8;
    for (auto i = 0; i < nthrs; i++) {
        thrs[i] = new std::thread(preload_thread, &set, i * count, count);
    }

    for (auto i = 0; i < nthrs; i++) {
        thrs[i]->join();
    }

    for (auto i = 0; i < nthrs; i++) {
        thrs[i] = new std::thread(read_thread, &set, i * count, count);
    }

    for (auto i = 0; i < nthrs; i++) {
        thrs[i]->join();
    }

    // Terminate libcds
    cds::Terminate();
}
