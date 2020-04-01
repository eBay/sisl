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
#include <memory>
#include <set>
#include <mutex>
#include <vector>

namespace sisl {
template < typename T >
struct urcu_node {
    rcu_head head;
    std::shared_ptr< T > val;

    template < typename... Args >
    urcu_node(Args&&... args) {
        val = std::make_shared< T >(std::forward< Args >(args)...);
    }

    std::shared_ptr< T > get() { return val; }

    void set(const T& v) { val = v; }
    static void free(struct rcu_head* rh) {
        auto* node = (urcu_node*)rh;
        delete node;
    }
};

template < typename T >
class urcu_ptr {
public:
    urcu_node< T >* m_gp;

    urcu_ptr(urcu_node< T >* gp) : m_gp(gp) { rcu_read_lock(); }
    ~urcu_ptr() { rcu_read_unlock(); }

    urcu_ptr(const urcu_ptr& other) = delete;
    urcu_ptr& operator=(const urcu_ptr& other) = delete;
    urcu_ptr(urcu_ptr&& other) noexcept { std::swap(m_gp, other.m_gp); }

    T* operator->() const {
        auto node = rcu_dereference(m_gp);
        return node->val.get();
    }

    T* get() const {
        auto node = rcu_dereference(m_gp);
        return node->val.get();
    }
};

template < typename T >
class urcu_data {
public:
    template < typename... Args >
    urcu_data(Args&&... args) {
        auto node = new urcu_node< T >(std::forward< Args >(args)...);
        rcu_assign_pointer(m_rcu_node, node);
        m_old_node = nullptr;
    }

    ~urcu_data() {
        delete (m_rcu_node);
        if (m_old_node != nullptr) { delete (m_old_node); }
    }

    template < typename... Args >
    std::shared_ptr< T > make_and_exchange(Args&&... args) {
        auto new_node = new urcu_node< T >(std::forward< Args >(args)...);
        auto old_rcu_node = m_rcu_node;
        rcu_assign_pointer(m_rcu_node, new_node);
        synchronize_rcu();
        auto ret = old_rcu_node->get();
        call_rcu(&old_rcu_node->head, urcu_node< T >::free);
        return ret;
    }

    template < typename... Args >
    void make(Args&&... args) {
        auto new_node = new urcu_node< T >(std::forward< Args >(args)...);
        m_old_node = m_rcu_node;
        rcu_assign_pointer(m_rcu_node, new_node);
    }

    std::shared_ptr< T > exchange() {
        if (m_old_node == nullptr) {
            /* exchange called without make for this thread. Its possible if a new thread is spawned between make and
             * exchange. Just return nullptr for this case */
            return nullptr;
        }
        auto ret = m_old_node->get();
        call_rcu(&m_old_node->head, urcu_node< T >::free);
        m_old_node = nullptr;
        return ret;
    }

    urcu_ptr< T > get() const {
        assert(m_rcu_node != nullptr);
        return urcu_ptr< T >(m_rcu_node);
    }

    urcu_node< T >* get_node() const { return m_rcu_node; }

    urcu_node< T >* m_rcu_node;
    urcu_node< T >* m_old_node; /* Used in case of make and exchange as 2 different steps */
};

template < typename T >
class urcu_data_batch {
public:
    static urcu_data_batch< T >& instance() {
        static urcu_data_batch< T > inst;
        return inst;
    }

    void add(urcu_data< T >* data) {
        std::lock_guard< std::mutex > lg(m_mutex);
        m_batch.insert(data);
    }

    void remove(urcu_data< T >* data) {
        std::lock_guard< std::mutex > lg(m_mutex);
        m_batch.insert(data);
    }

    template < typename... Args >
    void exchange(Args&&... args) {
        std::vector< urcu_node< T >* > old_nodes;
        old_nodes.reserve(m_batch.size());

        {
            std::lock_guard< std::mutex > lg(m_mutex);
            for (auto d : m_batch) {
                auto new_node = new urcu_node< T >(std::forward< Args >(args)...);
                old_nodes.push_back(d->m_rcu_node);
                rcu_assign_pointer(d->m_rcu_node, new_node);
            }

            synchronize_rcu();

            for (auto& on : old_nodes) {
                call_rcu(&on->head, urcu_node< T >::free);
            }
        }
    }

private:
    std::mutex m_mutex;
    std::set< urcu_data< T >* > m_batch;
};

class urcu_ctl {
public:
    static thread_local bool _rcu_registered_already;

    static void register_rcu() {
        if (!_rcu_registered_already) { rcu_register_thread(); }
    }

    static void declare_quiscent_state() { rcu_quiescent_state(); }

    static void unregister_rcu() { rcu_unregister_thread(); }

    static void sync_rcu() { synchronize_rcu(); }
};

#define RCU_REGISTER_INIT thread_local bool sisl::urcu_ctl::_rcu_registered_already = false
} // namespace sisl
