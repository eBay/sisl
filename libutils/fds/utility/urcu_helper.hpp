/*
 * urcu_helper.hpp
 *
 *  Created on: Sep 11, 2017
 *      Author: maditya
 */
#pragma once

#ifdef USE_CONCURRENT_DS
#include <cds/urcu/general_buffered.h>

typedef cds::urcu::gc<cds::urcu::general_buffered<>> urcu_gpb;
typedef typename urcu_gpb::scoped_lock urcu_lock;

template <typename T> struct UrcuHelper {

    std::atomic<T*> ptr;

    struct UrcuDisposer {
        void operator()(T* p) const { delete p; }
    };

    UrcuHelper(T* t) : ptr(t) {}

    /*
     * Access T under urcu_lock
     * For ex:
     * {
     *      urcu_lock l;
     *      T* t = helper.get()
     * }
     *
     * urcu_lock is quite lightweight, calls one atomic store operation.
     */
    T* get() { return ptr.load(); }

    /*
     * update function should not be called under read lock (urcu_lock)
     */
    void update(T* new_node) {
        T* old_node = ptr.exchange(new_node);
        urcu_gpb::retire_ptr<UrcuDisposer, T>(old_node);
    }
};
#endif


//#include <urcu.h>
#include <urcu-qsbr.h>


template <typename T>
struct urcu_node {
    rcu_head head;
    T val;

    template <typename... Args>
    urcu_node(Args&&... args) :
            val(std::forward< Args >(args)...) {
    }

    T *get() {
        return &val;
    }

    void set(const T &v) { val = v;}
    static void free(struct rcu_head* rh) {
        auto* node = (urcu_node*)rh;
        delete node;
    }
};

template <typename T>
class urcu_ptr {
public:
    urcu_node<T>* m_gp;

    urcu_ptr(urcu_node<T>* gp) : m_gp(gp) { rcu_read_lock(); }
    ~urcu_ptr() { rcu_read_unlock(); }

    urcu_ptr(const urcu_ptr& other) = delete;
    urcu_ptr& operator=(const urcu_ptr& other) = delete;
    urcu_ptr(urcu_ptr&& other) noexcept { std::swap(m_gp, other.m_gp); }

    T* operator->() const {
        auto node = rcu_dereference(m_gp);
        return &node->val;
    }

    T* get() const {
        auto node = rcu_dereference(m_gp);
        return &node->val;
    }
};

template <typename T>
class urcu_data {
public:
    template< typename... Args>
    urcu_data(Args&&... args) {
        auto node = new urcu_node<T>(std::forward< Args >(args)...);
        rcu_assign_pointer(m_rcu_node, node);
    }

    ~urcu_data() {
        delete(m_rcu_node);
    }

    template <typename... Args>
    urcu_node<T>* make_and_exchange(Args&&... args) {
        auto new_node = new urcu_node<T>(std::forward< Args >(args)...);
        auto old_rcu_node = m_rcu_node;
        rcu_assign_pointer(m_rcu_node, new_node);
        call_rcu(&old_rcu_node->head, urcu_node<T>::free);
        return old_rcu_node;
    }

    urcu_ptr<T> get() const {
        assert(m_rcu_node != nullptr);
        return urcu_ptr<T>(m_rcu_node);
    }

    urcu_node<T> *get_node() const {
        return m_rcu_node;
    }
private:
    urcu_node<T> *m_rcu_node;
};

class urcu_ctl {
public:
    static thread_local bool _rcu_registered_already;

    static void register_rcu() {
        assert(!_rcu_registered_already);
        rcu_register_thread();
        _rcu_registered_already = true;
    }

    static void declare_quiscent_state() {
        if (_rcu_registered_already) {
            rcu_quiescent_state();
        }
    }

    static void unregister_rcu() {
        assert(_rcu_registered_already);
        rcu_unregister_thread();
        _rcu_registered_already = false;
    }
};

#define RCU_REGISTER_INIT thread_local bool urcu_ctl::_rcu_registered_already = false
