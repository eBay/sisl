/*
 * test_bitobj_alloc.cpp
 *
 *  Created on: 18-May-2017
 *      Author: hkadayam
 */
#include "../mem/simple_bitmap_allocator.hpp"
#include <chrono>
#include <iostream>

using namespace std::chrono;
using namespace std;

struct Obj {
    Obj(int id) : m_id(id) {}
    Obj(int id, const char* name) : m_id(id), m_name(name) {}
    ~Obj() {
        // std::cout << "Obj destructor called" << std::endl;
    }
    int m_id;
    std::string m_name;
};

#define STATIC_POOL_SIZE 20
#define TOTAL_POOL_SIZE 10000
#define TOTAL_ALLOC_SIZE 20000
int main(int argc, char* argv[]) {
    fds::SimpleBitObjAllocator<Obj, STATIC_POOL_SIZE> balloc(TOTAL_POOL_SIZE);
    std::array<Obj*, TOTAL_ALLOC_SIZE> m_holder;

    high_resolution_clock::time_point tick, tock;
    tick = high_resolution_clock::now();
    for (auto i = 0; i < STATIC_POOL_SIZE; i++) {
        Obj* o = balloc.make_new(i, "validate");
        m_holder[i] = o;
    }
    tock = high_resolution_clock::now();
    std::cout << "Static pool allocation time for " << STATIC_POOL_SIZE << " = "
              << duration_cast<nanoseconds>(tock - tick).count() << " nsecs" << endl;

    tick = high_resolution_clock::now();
    for (auto i = STATIC_POOL_SIZE; i < TOTAL_POOL_SIZE; i++) {
        Obj* o = balloc.make_new(i, "validate");
        m_holder[i] = o;
    }
    tock = high_resolution_clock::now();
    std::cout << "Dynamic pool allocation time for " << TOTAL_POOL_SIZE - STATIC_POOL_SIZE << " = "
              << duration_cast<nanoseconds>(tock - tick).count() << " nsecs" << endl;

    tick = high_resolution_clock::now();
    for (auto i = TOTAL_POOL_SIZE; i < TOTAL_ALLOC_SIZE; i++) {
        Obj* o = balloc.make_new(i, "validate");
        m_holder[i] = o;
    }
    tock = high_resolution_clock::now();

    std::cout << "System allocation time for " << TOTAL_ALLOC_SIZE - TOTAL_POOL_SIZE << " = "
              << duration_cast<nanoseconds>(tock - tick).count() << " nsecs" << endl;

    for (auto i = 0; i < TOTAL_ALLOC_SIZE; i++) {
        balloc.dealloc(m_holder[i]);
    }
}
