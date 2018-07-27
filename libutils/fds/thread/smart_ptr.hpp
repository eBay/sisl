/*
 * smart_ptr.hpp
 *
 *  Created on: 20-Jan-2017
 *      Author: hkadayam
 */

#ifndef SMART_PTR_HPP_
#define SMART_PTR_HPP_

#pragma once

#include <cstddef>   // NULL
#include <algorithm> // std::swap
#include "fds.hpp"

// can be replaced by other error mechanism
#include <cassert>
#define SHARED_ASSERT(x) assert(x)

namespace fds {
#define DEBUG 1

template <typename T> class __smart_ptr {
  public:
    void init(mem_id_t id_m) {
        m_refcount.store(1);
        id_m.userdef = true; // Set as valid block;
        m_mem_id_val.store(id_m.to_uint32());
    }

    void acquire() {
        m_refcount.fetch_add(1);
        cout << "acquire, for " << this << " refcount = " << m_refcount.load() << endl;
    }

    bool release() {
        cout << "release for " << this << ", refcount = " << m_refcount.load() - 1 << endl;
        return (--m_refcount == 0);
    }

    void set_validity(bool is_valid) {
        mem_id_t id_m = mem_id_t::form(m_mem_id_val.load());
        id_m.userdef = is_valid;
        m_mem_id_val.store(id_m.to_uint32());
    }

    void set_validity_atomically(bool is_valid) {
        mem_id_t id_m;
        uint32_t id_val;

        do {
            id_val = m_mem_id_val.load();
            id_m = mem_id_t::form(id_val);
            id_m.userdef = is_valid;
        } while (!(m_mem_id_val.compare_exchange_weak(id_val, id_m.to_uint32())));
    }

    bool is_valid() {
        mem_id_t id_m = mem_id_t::form(m_mem_id_val.load());
        return (id_m.userdef == 1);
    }

    uint32_t use_count() { return m_refcount.load(); }

    mem_id_t get_mem_id() { return mem_id_t::form(m_mem_id_val.load()); }

  private:
    atomic<uint32_t> m_refcount;
    atomic<uint32_t> m_mem_id_val;
} __attribute__((packed));

/**
 * @brief Implementation of smart pointer, which also functions as a hazard pointer, atomic shared
 * pointer and native support to do linking (for singly linked list)
 *
 * smart_ptr is a smart pointer retaining ownership of an object through a provided pointer,
 * and sharing this ownership with a reference counter.
 * It destroys the object when the last shared pointer pointing to it is destroyed or reset.
 */
template <typename T> class smart_ptr {
  public:
    friend class __smart_ptr<T>;

    static_assert(sizeof(__smart_ptr<T>) == sizeof(mempool_header), "smart_ptr is not same size as mempool_header");

    smart_ptr() {
        m_ptr = nullptr;
        m_alloced = false;
    }

    smart_ptr(T* p) {
        mem_allocator* allocator = fds::mem_allocator::instance();
        mempool_header* hdr = allocator->to_hdr((uint8_t*)p);
        mem_id_t id_m = allocator->to_id((uint8_t*)p);

        m_ptr.store((__smart_ptr<T>*)hdr);
        m_ptr.load()->init(id_m);
        m_alloced = false;
    }

    smart_ptr(smart_ptr& other) {
        m_ptr.store(other.m_ptr.load());
        m_alloced = other.m_alloced;
        if (m_ptr.load() != nullptr) { m_ptr.load()->acquire(); }
    }

    template <typename... Args> static fds::smart_ptr<T> construct(Args&&... args) {
        // Allocate memory and construct the object
        uint8_t* mem = fds::malloc(sizeof(T));
        T* o = new (mem) T(std::forward<Args>(args)...);

        // Fold it with smart_ptr
        fds::smart_ptr<T> sptr(o);
        sptr.m_alloced = true;
        return sptr;
    }

    /// @brief the destructor releases its ownership and free if no one is referencing
    ~smart_ptr(void) {
        if ((m_ptr.load() != nullptr) && (m_ptr.load()->release() == true)) {
            T* rawptr = (T*)fds::mem_allocator::instance()->to_rawptr((mempool_header*)(m_ptr.load()));

            // If we alloced it, its our duty call placement delete destructor.
            if (m_alloced) { rawptr->~T(); }

            cout << "Freeing memory since refcount reached 0" << endl;
            fds::mem_allocator::instance()->free((uint8_t*)rawptr);
        }
    }

    smart_ptr(smart_ptr<T>&& other) {
        if (other.m_ptr.load() != nullptr) { other.m_ptr.load()->acquire(); }
        m_ptr.store(other.m_ptr.load());
        m_alloced = other.m_alloced;
    }

    /// @brief Assignment operator using the copy-and-swap idiom (copy constructor and swap method)
    smart_ptr<T>& operator=(const smart_ptr<T>& other) {
        // If I am pointing to something valid, it needs to be released.
        if (m_ptr.load() != nullptr) {
            if (m_ptr.load()->release() == true) {
                cout << "Freeing memory since refcount reached 0" << endl;
                fds::mem_allocator::instance()->free((mempool_header*)(m_ptr.load()));
            }
        }

        m_ptr.store(other.m_ptr.load());
        m_alloced = other.m_alloced;
        if (m_ptr.load() != nullptr) {
            // Acquire the other block
            m_ptr.load()->acquire();
        }
        return *this;
    }

    /// @brief this reset releases its ownership
    inline void reset(void) throw() // never throws
    {
        if (m_ptr->release() == true) { fds::mem_allocator::instance()->free((mempool_header*)m_ptr.load()); }
    }

    void set_validity(bool is_valid, bool is_atomic = false) {
        if (m_ptr) {
            is_atomic ? m_ptr.load()->set_validity(is_valid) : m_ptr.load()->set_validity_atomically(is_valid);
        }
    }

    bool is_valid() { return (m_ptr ? m_ptr.load()->is_valid() : false); }

#if 0
     /// @brief this reset release its ownership and re-acquire another one
     void reset(T* p) // may throw std::bad_alloc
     {
         SHARED_ASSERT((NULL == p) || (m_rawptr != p)); // auto-reset not allowed
         release();
         acquire(p); // may throw std::bad_alloc
     }

     /// @brief Swap method for the copy-and-swap idiom (copy constructor and swap method)
     void swap(atomic_smart_ptr& lhs) throw() // never throws
     {
         std::swap(m_rawptr, lhs.m_rawptr);
         pn.swap(lhs.pn);
     }
#endif

#if 0
     bool compare_and_swap(atomic_smart_ptr &prev, atomic_smart_ptr &cur)
     {
     	m_raw_ptr.compare_exchange_weak(prev.m_raw_ptr, cur.m_raw_ptr);
     }
#endif

#if 0
     // reference counter operations :
     inline operator bool() const throw() // never throws
     {
         return (0 < pn.use_count());
     }
     inline bool unique(void)  const throw() // never throws
     {
         return (1 == pn.use_count());
     }
     long use_count(void)  const throw() // never throws
     {
         return pn.use_count();
     }
#endif

    // underlying pointer operations :
    inline T& operator*() {
        // TODO: Throw excepton if m_ptr is a nullptr;
        mempool_header* hdr = (mempool_header*)(m_ptr.load());
        T* p = (T*)fds::mem_allocator::instance()->to_rawptr(hdr);
        return (T&)(*p);
    }

    inline T* operator->() {
        // TODO: Throw exception if it is a nullptr;
        mempool_header* hdr = (mempool_header*)(m_ptr.load());
        return (T*)fds::mem_allocator::instance()->to_rawptr(hdr);
    }

    inline T* get(void) {
        if (m_ptr == nullptr) { return nullptr; }

        // no assert, can return NULL
        mempool_header* hdr = (mempool_header*)(m_ptr.load());
        return (T*)fds::mem_allocator::instance()->to_rawptr(hdr);
    }

    bool cas(const fds::smart_ptr<T>& oldp, const fds::smart_ptr<T>& newp) {
        bool status = false;

        __smart_ptr<T>* old_ptr = oldp->m_ptr.load();

        // Acquire the new ptr and do a atomic swap.
        newp.m_ptr->acquire();
        status = m_ptr->compare_exchange_weak(old_ptr, newp.m_ptr);
        if (status) {
            // We need to release old_ptr and free if need be
            if (old_ptr->release() == true) {
                cout << "Freeing memory since refcount reached 0" << endl;
                fds::mem_allocator::instance()->free((mempool_header*)old_ptr);
            }
        } else {
            bool ret = newp.m_ptr->release();
            assert(ret == false); // We just inc the ref, we should not be 0.
        }

        return status;
    }

  private:
    atomic<__smart_ptr<T>*> m_ptr;
    bool m_alloced;
};

// comparaison operators
template <class T, class U>
inline bool operator==(const smart_ptr<T>& l, const smart_ptr<U>& r) throw() // never throws
{
    return (l.get() == r.get());
}
template <class T, class U>
inline bool operator!=(const smart_ptr<T>& l, const smart_ptr<U>& r) throw() // never throws
{
    return (l.get() != r.get());
}
template <class T, class U>
inline bool operator<=(const smart_ptr<T>& l, const smart_ptr<U>& r) throw() // never throws
{
    return (l.get() <= r.get());
}
template <class T, class U>
inline bool operator<(const smart_ptr<T>& l, const smart_ptr<U>& r) throw() // never throws
{
    return (l.get() < r.get());
}
template <class T, class U>
inline bool operator>=(const smart_ptr<T>& l, const smart_ptr<U>& r) throw() // never throws
{
    return (l.get() >= r.get());
}
template <class T, class U>
inline bool operator>(const smart_ptr<T>& l, const smart_ptr<U>& r) throw() // never throws
{
    return (l.get() > r.get());
}

#if 0
// static cast of smart_ptr
template<class T, class U>
atomic_smart_ptr<T> static_pointer_cast(const smart_ptr<U>& ptr) // never throws
{
    return smart_ptr<T>(ptr, static_cast<typename smart_ptr<T>::element_type*>(ptr.get()));
}

// dynamic cast of smart_ptr
template<class T, class U>
smart_ptr<T> dynamic_pointer_cast(const smart_ptr<U>& ptr) // never throws
{
    T* p = dynamic_cast<typename smart_ptr<T>::element_type*>(ptr.get());
    if (NULL != p)
    {
        return smart_ptr<T>(ptr, p);
    }
    else
    {
        return smart_ptr<T>();
    }
}
#endif
} // namespace fds
#endif /* SMART_PTR_HPP_ */
