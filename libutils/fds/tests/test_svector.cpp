/*
 * test_svector.cpp
 *
 *  Created on: 13-May-2017
 *      Author: hkadayam
 */

#include "../list/sorted_vector_set.hpp"
#include <iostream>
#include <chrono>
#include <ctime>

using namespace std;
using namespace std::chrono;

struct txn {
    txn(int id) { m_id = id; }
    ~txn() {
        // std::cout << "Destructor of txn called" << std::endl;
    }
    int m_id;
};

struct txn_lesser_check {
    inline bool operator()(const txn& s1, const txn& s2) { return (s1.m_id < s2.m_id); }

    inline bool operator()(const txn& s1, int id) { return (s1.m_id < id); }

    inline bool operator()(int id1, int id2) { return (id1 < id2); }
};

struct txn_equal_check {
    inline bool operator()(const txn& s1, const txn& s2) { return (s1.m_id == s2.m_id); }

    inline bool operator()(const txn& s1, int id) { return (s1.m_id == id); }

    inline bool operator()(int id1, int id2) { return (id1 == id2); }
};

#define PRELOAD_COUNT 50000
#define DELETE_COUNT 40000
int main(int argc, char* argv[]) {
    fds::SortedVectorSet<int, txn, txn_lesser_check, txn_equal_check> svec(PRELOAD_COUNT * 3, PRELOAD_COUNT * 9);
    high_resolution_clock::time_point tick, tock;

    std::vector<txn> inp_vec;
    for (auto i = 0; i < PRELOAD_COUNT; i++) {
        inp_vec.emplace_back(i);
    }

    tick = high_resolution_clock::now();
    for (auto i = 0; i < PRELOAD_COUNT; i++) {
        bool inserted = svec.insert_from_back(i, inp_vec[i]);
        assert(inserted);
    }
    tock = high_resolution_clock::now();
    std::cout << "Preload    for " << PRELOAD_COUNT << " = " << duration_cast<nanoseconds>(tock - tick).count()
              << " nsecs" << endl;

#if 0
    for (auto i = 0; i < 50; i++) {
        bool inserted = svec.insert_from_back(i, inp_vec[i]);
        assert(!inserted);
    }
#endif

    // using built-in random generator to shuffle.
    std::random_shuffle(inp_vec.begin(), inp_vec.end());

    tick = high_resolution_clock::now();
    for (auto i = 0; i < DELETE_COUNT; i++) {
        txn pt;
        int k = inp_vec[i].m_id;
        bool deleted = svec.extract(k, &pt);
        assert(deleted);
        // if (deleted) {
        // std::cout << "Deleted id = " << pt->m_id << std::endl;
        //}
    }
    tock = high_resolution_clock::now();
    std::cout << "Delete     for " << DELETE_COUNT << " = " << duration_cast<nanoseconds>(tock - tick).count()
              << " nsecs" << endl;

    tick = high_resolution_clock::now();
    for (auto i = 0; i < PRELOAD_COUNT; i++) {
        txn pt;
        int k = rand() % 100;
        bool found = svec.find(k, &pt);
        if (found) {
            // std::cout << "Found id = " << pt->m_id << std::endl;
        } else {
            // std::cout << "Id " << k << " not found" << std::endl;
        }
    }
    tock = high_resolution_clock::now();
    std::cout << "Find       for " << PRELOAD_COUNT << " = " << duration_cast<nanoseconds>(tock - tick).count()
              << " nsecs" << endl;

#ifdef DEBUG_PRINTS
    std::cout << "Before compaction: " << std::endl;
    auto itend = svec.end();
    for (auto it = svec.begin(); it != itend; ++it) {
        std::cout << (*it)->m_id << ", ";
    }
    std::cout << std::endl;
#endif

    tick = high_resolution_clock::now();
    svec.compact();
    tock = high_resolution_clock::now();
    std::cout << "Compaction for " << DELETE_COUNT << " = " << duration_cast<nanoseconds>(tock - tick).count()
              << " nsecs" << endl;

#ifdef DEBUG_PRINTS
    std::cout << "After compaction: " << std::endl;
    itend = svec.end();
    for (auto it = svec.begin(); it != itend; ++it) {
        std::cout << (*it)->m_id << ", ";
    }
    std::cout << std::endl;
#endif
}
