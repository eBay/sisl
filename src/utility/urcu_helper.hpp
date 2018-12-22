/*
 * urcu_helper.hpp
 *
 *  Created on: Sep 11, 2017
 *      Author: maditya
 */
#pragma once

#include <urcu.h>
#include <urcu/static/urcu-qsbr.h>
#include <urcu-call-rcu.h>

namespace sisl {
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
        rcu_register_thread();
    }

    static void declare_quiscent_state() {
        rcu_quiescent_state();
    }

    static void unregister_rcu() {
        rcu_unregister_thread();
    }
};

#define RCU_REGISTER_INIT thread_local bool sisl::urcu_ctl::_rcu_registered_already = false
}
