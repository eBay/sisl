/*
 * simple_bitmap_allocator.hpp
 *
 *  Created on: 17-May-2017
 *      Author: hkadayam
 */

#ifndef SRC_LIBUTILS_FDS_MEM_SIMPLE_BITMAP_ALLOCATOR_HPP_
#define SRC_LIBUTILS_FDS_MEM_SIMPLE_BITMAP_ALLOCATOR_HPP_

#include <boost/dynamic_bitset.hpp>
#include <array>
#include <iostream>

namespace fds {
/*
 * This is a simple bitmap based memory allocator. It does not guarantee thread safety. Bitmap
 * based allocator is superior to free list based allocator for use cases where there is no
 * need to maintain this allocator for longer time. It also helps L1 cache locality with bitmap
 * being cache friendly.
 *
 * Another advantage is it does not need extensive metadata list for memory and hence very little
 * overhead. One more advantage its initialization is practically O(1) compared to approach of
 * creating a pool of freelist, hence particulary suiting use cases for short lived memory pool.
 *
 * Allocation is not always O(1), but scanning a bitmap is extremely fast. Its safe to allocate
 * more than what is provided in the constructor, in which case it reverts to malloc/free.
 *
 */
template <typename T, size_t StaticCount> class SimpleBitObjAllocator {
  public:
    SimpleBitObjAllocator(uint32_t count) : m_allocbits(0), m_count(count), m_objpool_dynamic(nullptr) {
        if (count > StaticCount) { m_objpool_dynamic = new uint8_t[sizeof(T) * (count - StaticCount)]; }
        m_allocbits.resize(count, true);
        m_freeind = m_allocbits.find_first();
    }

    ~SimpleBitObjAllocator() {
        if (m_objpool_dynamic != nullptr) { delete (m_objpool_dynamic); }
    }

    template <class... Args> T* make_new(Args&&... args) {
        size_t ind = find_next_slot();
        void* mem;

        if (ind == boost::dynamic_bitset<>::npos) {
            mem = malloc(sizeof(T));
            if (mem == nullptr) { throw std::bad_alloc(); }
        } else {
            m_allocbits[ind] = false;

            if (ind < StaticCount) {
                mem = (void*)(m_objpool_static + (sizeof(T) * ind));
            } else {
                mem = (void*)(m_objpool_dynamic + (sizeof(T) * (ind - StaticCount)));
            }
        }

        return new (mem) T(std::forward<Args>(args)...);
    }

    void dealloc(T* val) {
        size_t ind = _owns((uint8_t*)val);
        val->~T();

        if (ind == boost::dynamic_bitset<>::npos) {
            free(val);
        } else {
            m_allocbits[ind] = true;
        }
    }

    bool owns(T* val) { return (_owns((uint8_t*)val) != boost::dynamic_bitset<>::npos); }

  private:
    size_t find_next_slot() {
        m_freeind = m_allocbits.find_next(m_freeind);
        if (m_freeind == boost::dynamic_bitset<>::npos) {
            m_freeind = m_allocbits.find_first();
            // std::cout << "Reached end, starting again with next slot = " << m_freeind << std::endl;
        }
        return m_freeind;
    }

    size_t _owns(uint8_t* ptr) {
        if ((ptr >= m_objpool_static) && (ptr <= m_objpool_static + (sizeof(T) * StaticCount))) {
            return (ptr - m_objpool_static) / sizeof(T);
        } else if ((ptr >= m_objpool_dynamic) && (ptr < m_objpool_dynamic + (sizeof(T) * (m_count - StaticCount)))) {
            return (ptr - m_objpool_dynamic) / sizeof(T) + StaticCount;
        } else {
            return boost::dynamic_bitset<>::npos;
        }
    }

  private:
    boost::dynamic_bitset<> m_allocbits;
    uint8_t m_objpool_static[sizeof(T) * StaticCount];
    uint8_t* m_objpool_dynamic;
    uint32_t m_count;

    size_t m_freeind;
};

} // namespace fds

#endif /* SRC_LIBUTILS_FDS_MEM_SIMPLE_BITMAP_ALLOCATOR_HPP_ */
