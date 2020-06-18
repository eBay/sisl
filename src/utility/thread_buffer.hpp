//
// Created by Kadayam, Hari on 31/05/17.
//

#pragma once

#include <memory>
#include <mutex>
#include <condition_variable>
#include <boost/dynamic_bitset.hpp>
#include <functional>
#include "fds/flexarray.hpp"
#include "fds/sparse_vector.hpp"
#include "urcu_helper.hpp"
#include <cxxabi.h>
#include <cstdio>
#include <shared_mutex>
#include "utility/atomic_counter.hpp"

namespace sisl {

enum thread_life_cycle { THREAD_ATTACHED, THREAD_DETACHED };

typedef std::function< void(uint32_t, thread_life_cycle) > thread_state_cb_t;

#if 0
template <typename T>
struct atomic_wrapper {
    std::atomic<T> m_av;

    atomic_wrapper() : m_av() {}
    atomic_wrapper(const T& v) : m_av(v) {}
    atomic_wrapper(const std::atomic<T> &a) :m_av(m_av.load(std::memory_order_acquire)) {}
    atomic_wrapper(const atomic_wrapper &other) :m_av(other.m_av.load()) {}
    atomic_wrapper& operator=(const atomic_wrapper &other) {
        m_av.store(other.m_av.load(std::memory_order_acquire), std::memory_order_release);
    }
};
#endif

class ThreadRegistry {
#define INVALID_CURSOR boost::dynamic_bitset<>::npos

    typedef std::map< uint64_t, thread_state_cb_t > notifiers_list_t;

public:
    static constexpr size_t max_tracked_threads() { return 2048U; }

    ThreadRegistry() :
            m_free_thread_slots(max_tracked_threads()),
            // m_refed_slots(MAX_TRACKED_THREADS),
            m_ref_count(max_tracked_threads(), 0) {
        // Mark all slots as free
        m_free_thread_slots.set();
        m_slot_cursor = INVALID_CURSOR;
    }

    uint32_t attach() {
        uint32_t thread_num;
        notifiers_list_t notifiers;

        {
            std::unique_lock lock(m_init_mutex);

            // Wrap around to get the next free slot
            thread_num = get_next_free_slot();

            // Mark the slot as not free
            m_free_thread_slots.reset(thread_num);

            char thread_name[256] = {0};
            size_t len = sizeof(thread_name);

#ifdef _POSIX_THREADS
            pthread_getname_np(pthread_self(), thread_name, len);
#endif /* _POSIX_THREADS */
            sisl::urcu_ctl::register_rcu();

            notifiers = m_registered_notifiers;
            ++m_ongoing_notifications;
        }

        // Notify the modules that a new thread is attached
        for (auto& n : notifiers) {
            n.second(thread_num, thread_life_cycle::THREAD_ATTACHED);
        }

        finish_notification();
        return thread_num;
    }

    void detach(uint32_t thread_num) {
        notifiers_list_t notifiers;
        {
            std::unique_lock lock(m_init_mutex);
            m_free_thread_slots.set(thread_num);
            notifiers = m_registered_notifiers;
            ++m_ongoing_notifications;
        }

        for (auto& n : notifiers) {
            n.second(thread_num, thread_life_cycle::THREAD_DETACHED);
        }

        finish_notification();
        sisl::urcu_ctl::unregister_rcu();
    }

    void slot_inc_ref(uint32_t thread_num) { m_ref_count[thread_num].increment(); }

    void slot_release(uint32_t thread_num) { m_ref_count[thread_num].decrement_testz(); }

    uint64_t register_for_sc_notification(const thread_state_cb_t& cb) {
        std::vector< uint32_t > tnums;
        uint64_t notify_idx;

        tnums.reserve(m_free_thread_slots.size());
        {
            std::unique_lock lock(m_init_mutex);
            notify_idx = m_next_notify_idx++;
            m_registered_notifiers[notify_idx] = cb;
            // m_registered_notifiers.push_back(cb);

            // We need to make a callback to this registeree with all running threads
            auto running_thread_slots = ~m_free_thread_slots;
            auto tnum = running_thread_slots.find_first();
            while (tnum != INVALID_CURSOR) {
                tnums.push_back(tnum);
                tnum = running_thread_slots.find_next(tnum);
            }
            ++m_ongoing_notifications;
        }

        for (auto tnum : tnums) {
            cb(tnum, thread_life_cycle::THREAD_ATTACHED);
        }
        finish_notification();
        return notify_idx;
    }

    void deregister_sc_notification(uint64_t notify_idx) {
        std::unique_lock lock(m_init_mutex);
        m_registered_notifiers.erase(notify_idx);
        if (m_ongoing_notifications != 0) {
            m_notify_cv.wait(lock, [&] { return (m_ongoing_notifications == 0); });
        }
    }

    bool is_thread_running(uint32_t thread_num) {
        std::shared_lock lock(m_init_mutex);
        return !m_free_thread_slots[thread_num];
    }

    static ThreadRegistry* instance() { return &inst; }
    static ThreadRegistry inst;

private:
    uint32_t get_next_free_slot() {
        do {
            if (m_slot_cursor == INVALID_CURSOR) {
                m_slot_cursor = m_free_thread_slots.find_first();
                if (m_slot_cursor == INVALID_CURSOR) {
                    throw std::invalid_argument("Number of threads exceeded max limit");
                }
            } else {
                m_slot_cursor = m_free_thread_slots.find_next(m_slot_cursor);
            }
        } while ((m_slot_cursor == INVALID_CURSOR) || (!m_ref_count[m_slot_cursor].testz()));

        return (uint32_t)m_slot_cursor;
    }

    void finish_notification() {
        bool ongoing_notifications = false;
        {
            std::unique_lock lock(m_init_mutex);
            ongoing_notifications = (--m_ongoing_notifications > 0);
        }

        if (!ongoing_notifications) { m_notify_cv.notify_all(); }
    }

private:
    std::shared_mutex m_init_mutex;

    // A bitset where 1 marks for free thread slot, 0 for not free
    boost::dynamic_bitset<> m_free_thread_slots;

    // Next thread free slot
    boost::dynamic_bitset<>::size_type m_slot_cursor;

    // Number of buffers that are open for a given thread
    // boost::dynamic_bitset<> m_refed_slots;
    std::vector< sisl::atomic_counter< int > > m_ref_count;

    uint32_t m_next_notify_idx = 0;
    notifiers_list_t m_registered_notifiers;
    std::condition_variable_any m_notify_cv;
    int32_t m_ongoing_notifications = 0;
    // std::vector< thread_state_cb_t >        m_registered_notifiers;
};

#define thread_registry ThreadRegistry::instance()

class ThreadLocalContext {
public:
    ThreadLocalContext() {
        this_thread_num = thread_registry->attach();
        // LOGINFO("Created new ThreadLocalContext with thread_num = {}, my_thread_num = {}", this_thread_num,
        //        my_thread_num());
    }

    ~ThreadLocalContext() {
        thread_registry->detach(this_thread_num);
        this_thread_num = (uint32_t)-1;
    }

    static ThreadLocalContext* instance() { return &inst; }

    static uint32_t my_thread_num() { return instance()->this_thread_num; }
    static void set_context(uint32_t context_id, uint64_t context) { instance()->user_contexts[context_id] = context; }
    static uint64_t get_context(uint32_t context_id) { return instance()->user_contexts[context_id]; }

    static thread_local ThreadLocalContext inst;

    uint32_t this_thread_num;
    std::array< uint64_t, 5 > user_contexts; // To store any user contexts
};

#define THREAD_BUFFER_INIT                                                                                             \
    sisl::ThreadRegistry sisl::ThreadRegistry::inst;                                                                   \
    thread_local sisl::ThreadLocalContext sisl::ThreadLocalContext::inst;

template < typename T >
using buffer_access_cb_v1_t = std::function< bool(T*, bool) >;

template < typename T >
using buffer_access_cb_v2_t = std::function< bool(T*, bool, bool) >;

template < typename T >
using exit_safe_buffer_access_cb_t = std::variant< buffer_access_cb_v1_t< T >, buffer_access_cb_v2_t< T > >;

template < bool IsActiveThreadsOnly, typename T, typename... Args >
class ThreadBuffer {
public:
    template < class... Args1 >
    // ThreadBuffer(Args1&&... args) : m_args(std::make_tuple(std::forward<Args1>(args)...) {
    // ThreadBuffer(Args1&&... args) : m_args(std::forward_as_tuple((args)...)) {
    ThreadBuffer(Args1&&... args) :
            m_args(std::forward< Args1 >(args)...),
            m_thread_slots(ThreadRegistry::max_tracked_threads()) {
        m_buffers.reserve(ThreadRegistry::max_tracked_threads());
        m_notify_idx = thread_registry->register_for_sc_notification(
            std::bind(&ThreadBuffer::on_thread_state_change, this, std::placeholders::_1, std::placeholders::_2));
    }

    ~ThreadBuffer() { thread_registry->deregister_sc_notification(m_notify_idx); }

    T* get() {
        auto tnum = ThreadLocalContext::my_thread_num();
        assert(m_buffers[tnum].get() != nullptr);
        return m_buffers[tnum].get();
    }

    const T* get() const {
        auto tnum = ThreadLocalContext::my_thread_num();
        assert(m_buffers[tnum].get() != nullptr);
        return m_buffers[tnum].get();
    }

    T& operator*() { return *(get()); }
    const T& operator*() const { return *(get()); }

    T* operator->() { return get(); }
    const T* operator->() const { return get(); }

    T* operator[](uint32_t n) {
        assert(n < get_count());
        return m_buffers[n];
    }
    const T* operator[](uint32_t n) const {
        assert(n < get_count());
        return m_buffers[n];
    }

    void on_thread_state_change(uint32_t thread_num, thread_life_cycle change) {
        switch (change) {
        case thread_life_cycle::THREAD_ATTACHED: {
            std::unique_lock l(m_expand_mutex);
            create_buffer(thread_num, m_args, std::index_sequence_for< Args... >());
            break;
        }
        case thread_life_cycle::THREAD_DETACHED:
            if (IsActiveThreadsOnly) {
                std::unique_lock l(m_expand_mutex);
                m_buffers.at(thread_num).reset();
                m_thread_slots.reset(thread_num);
                thread_registry->slot_release(thread_num);
            }
            break;
        }
    }

    uint32_t get_count() const { return m_buffers.size(); }

    void access_all_threads(exit_safe_buffer_access_cb_t< T > cb) {
        std::vector< uint32_t > can_free_thread_bufs;
        {
            std::shared_lock l(m_expand_mutex);
            auto tnum = m_thread_slots.find_first();
            while (tnum != INVALID_CURSOR) {
                auto is_running = IsActiveThreadsOnly || thread_registry->is_thread_running(tnum);
                bool can_free = false;
                auto next_tnum = m_thread_slots.find_next(tnum);
                if (std::holds_alternative< buffer_access_cb_v1_t< T > >(cb)) {
                    can_free =
                        std::get< buffer_access_cb_v1_t< T > >(cb)(m_buffers.at(tnum).get(), is_running) && !is_running;
                } else if (std::holds_alternative< buffer_access_cb_v2_t< T > >(cb)) {
                    can_free = std::get< buffer_access_cb_v2_t< T > >(cb)(m_buffers.at(tnum).get(), is_running,
                                                                          (next_tnum == INVALID_CURSOR)) &&
                        !is_running;
                }
                if (can_free) { can_free_thread_bufs.push_back(tnum); }
                tnum = next_tnum;
            }
        }

        // We have some threads which have exited and caller allowed to free this buffer, free the slot
        // and reduce the ref count in the registry
        if (can_free_thread_bufs.size()) {
            std::unique_lock l(m_expand_mutex);
            for (auto i : can_free_thread_bufs) {
                m_buffers.at(i) = nullptr;
                m_thread_slots.reset(i);
                thread_registry->slot_release(i);
            }
        }
    }

    bool access_specific_thread(uint32_t thread_num, std::function< bool(T*, bool) > cb) {
        bool can_free = false;
        {
            std::shared_lock l(m_expand_mutex);
            // If thread is not running or its context is already expired, then no callback.
            if (!m_thread_slots[thread_num]) { return false; }

            auto is_running = IsActiveThreadsOnly || thread_registry->is_thread_running(thread_num);
            can_free = cb(m_buffers.at(thread_num).get(), is_running) && !is_running;
        }

        if (can_free) {
            std::unique_lock l(m_expand_mutex);
            m_buffers.at(thread_num) = nullptr;
            m_thread_slots.reset(thread_num);
            thread_registry->slot_release(thread_num);
        }

        return true;
    }

    void reset() { m_buffers[ThreadLocalContext::my_thread_num()].reset(); }

private:
    template < std::size_t... Is >
    void create_buffer(uint32_t tnum, const std::tuple< Args... >& tuple, std::index_sequence< Is... >) {
        m_buffers[tnum] = std::make_unique< T >(std::get< Is >(tuple)...);
        thread_registry->slot_inc_ref(tnum);
        m_thread_slots[tnum] = true;
    }

private:
    sisl::sparse_vector< std::unique_ptr< T > > m_buffers;
    std::tuple< Args... > m_args;
    std::shared_mutex m_expand_mutex;
    boost::dynamic_bitset<> m_thread_slots;
    uint64_t m_notify_idx = 0;
};

template < typename T, typename... Args >
using ExitSafeThreadBuffer = ThreadBuffer< false, T, Args... >;

template < typename T >
using buffer_access_cb_v3_t = std::function< void(T*) >;

template < typename T >
using buffer_access_cb_v4_t = std::function< void(T*, bool) >;

template < typename T >
using active_only_buffer_access_cb_t = std::variant< buffer_access_cb_v3_t< T >, buffer_access_cb_v4_t< T > >;

template < typename T, typename... Args >
class ActiveOnlyThreadBuffer : public ThreadBuffer< true, T, Args... > {
public:
    ActiveOnlyThreadBuffer(Args&&... args) : ThreadBuffer< true, T, Args... >(std::forward< Args >(args)...) {}

    void access_all_threads(active_only_buffer_access_cb_t< T > cb) {
        if (std::holds_alternative< buffer_access_cb_v3_t< T > >(cb)) {
            return ThreadBuffer< true, T, Args... >::access_all_threads(
                [&](T* t, [[maybe_unused]] bool is_thread_running) {
                    assert(is_thread_running);
                    std::get< buffer_access_cb_v3_t< T > >(cb)(t);
                    return false;
                });
        } else {
            return ThreadBuffer< true, T, Args... >::access_all_threads(
                [&](T* t, [[maybe_unused]] bool is_thread_running, bool is_last_thread) {
                    assert(is_thread_running);
                    std::get< buffer_access_cb_v4_t< T > >(cb)(t, is_last_thread);
                    return false;
                });
        }
    }

    bool access_specific_thread(uint32_t thread_num, const std::function< void(T*) >& cb) {
        return ThreadBuffer< true, T, Args... >::access_specific_thread(
            thread_num, [&](T* t, [[maybe_unused]] bool is_thread_running) {
                assert(is_thread_running);
                cb(t);
                return false;
            });
    }
};

} // namespace sisl
