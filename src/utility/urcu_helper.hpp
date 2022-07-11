/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam, Aditya Marella
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#pragma once

#if __cplusplus <= 201703L
#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <urcu.h>
#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic pop
#endif
#else
#include <urcu.h>
#endif

#include <memory>
#include <set>
#include <mutex>
#include <vector>
#include <tuple>

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

    static void register_rcu() { rcu_register_thread(); }

    static void unregister_rcu() { rcu_unregister_thread(); }

    static void sync_rcu() { synchronize_rcu(); }
};

/* TODO : Try an ensure the lifetime of this object does not exceed the scoped_ptr that created it */
template < typename T >
class _urcu_access_ptr {

public:
    T* m_p;

    _urcu_access_ptr(T* p) : m_p(p) {}
    ~_urcu_access_ptr() { rcu_read_unlock(); }
    _urcu_access_ptr(const _urcu_access_ptr& other) = delete;
    _urcu_access_ptr& operator=(const _urcu_access_ptr& other) = delete;
    _urcu_access_ptr(_urcu_access_ptr&& other) noexcept { std::swap(m_p, other.m_p); }

    const T* operator->() const { return m_p; }
    T* operator->() { return m_p; }
    T* get() const { return m_p; }
};

/* Simplified urcu pointer access */
template < typename T, typename... Args >
class urcu_scoped_ptr {
public:
    template < class... Args1 >
    urcu_scoped_ptr(Args1&&... args) : m_args(std::forward< Args1 >(args)...) {
        m_cur_obj = new T(std::forward< Args1 >(args)...);
    }

    urcu_scoped_ptr(urcu_scoped_ptr const&) = delete;
    urcu_scoped_ptr(urcu_scoped_ptr&&) = delete;
    urcu_scoped_ptr& operator=(urcu_scoped_ptr const&) = delete;
    urcu_scoped_ptr& operator=(urcu_scoped_ptr&&) = delete;

    ~urcu_scoped_ptr() {
        rcu_read_lock(); // Take read-fence prior to accessing m_cur_obj
        if (m_cur_obj) delete rcu_dereference(m_cur_obj);
        rcu_read_unlock();
    }

    void read(const auto& cb) const {
        rcu_read_lock();
        auto s = rcu_dereference(m_cur_obj);
        cb((const T*)s);
        rcu_read_unlock();
    }

    _urcu_access_ptr< T > access() const {
        rcu_read_lock();                                          // Take read-fence prior to accessing m_cur_obj
        return _urcu_access_ptr< T >(rcu_dereference(m_cur_obj)); // This object will read_unlock when it's destroyed
    }

    void update(const auto& edit_cb) {
        T* old_obj{nullptr};
        {
            auto l = std::scoped_lock(m_updater_mutex);        // TODO: Should we have it here or leave it to caller???
            auto new_obj = new T(*rcu_dereference(m_cur_obj)); // Create new obj from old obj
            edit_cb(new_obj);

            old_obj = rcu_xchg_pointer(&m_cur_obj, new_obj);
        }
        synchronize_rcu();
        if (old_obj) [[likely]]
            delete old_obj;
    }

    T* make_and_exchange(const bool sync_rcu_now = true) {
        return _make_and_exchange(sync_rcu_now, m_args, std::index_sequence_for< Args... >());
    }

private:
    template < std::size_t... Is >
    T* _make_and_exchange(const bool sync_rcu_now, const std::tuple< Args... >& tuple, std::index_sequence< Is... >) {
        auto new_obj = new T(std::get< Is >(tuple)...); // Make new object with saved constructor params
        auto old_obj = rcu_xchg_pointer(&m_cur_obj, new_obj);
        if (sync_rcu_now) { synchronize_rcu(); }
        return old_obj;
    }

private:
    // Args to hold onto for new buf
    std::tuple< Args... > m_args;

    // RCU protected pointer
    T* m_cur_obj = nullptr;

    // Mutex to protect multiple copiers to run the copy step in parallel.
    std::mutex m_updater_mutex;
};

#define RCU_REGISTER_INIT thread_local bool sisl::urcu_ctl::_rcu_registered_already = false;
} // namespace sisl
