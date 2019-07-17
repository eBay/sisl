//
// Created by Kadayam, Hari on 31/05/17.
//

#pragma once

#include <memory>
#include <mutex>
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

public:
    static constexpr size_t max_tracked_threads() { return 1024U; }

    ThreadRegistry() :
            m_free_thread_slots(max_tracked_threads()),
            // m_refed_slots(MAX_TRACKED_THREADS),
            m_ref_count(max_tracked_threads(), 0) {
        // Mark all slots as free
        m_free_thread_slots.set();
        m_slot_cursor = INVALID_CURSOR;
    }

    uint32_t attach() {
        uint32_t                         thread_num;
        std::vector< thread_state_cb_t > notifiers;

        {
            std::unique_lock lock(m_init_mutex);

            // Wrap around to get the next free slot
            thread_num = get_next_free_slot();

            // Mark the slot as not free
            m_free_thread_slots.reset(thread_num);

            char   thread_name[256] = {0};
            size_t len = sizeof(thread_name);

#ifdef _POSIX_THREADS
            pthread_getname_np(pthread_self(), thread_name, len);
#endif /* _POSIX_THREADS */
            sisl::urcu_ctl::register_rcu();

            notifiers = m_registered_notifiers;
        }

        // Notify the modules that a new thread is attached
        for (auto cb : notifiers) {
            cb(thread_num, thread_life_cycle::THREAD_ATTACHED);
        }
        return thread_num;
    }

    void detach(uint32_t thread_num) {
        std::vector< thread_state_cb_t > notifiers;
        {
            std::unique_lock lock(m_init_mutex);
            m_free_thread_slots.set(thread_num);
            notifiers = m_registered_notifiers;
        }

        for (auto cb : notifiers) {
            cb(thread_num, thread_life_cycle::THREAD_DETACHED);
        }
        sisl::urcu_ctl::unregister_rcu();
    }

    void slot_inc_ref(uint32_t thread_num) { m_ref_count[thread_num].increment(); }

    void slot_release(uint32_t thread_num) { m_ref_count[thread_num].decrement_testz(); }

    void register_for_sc_notification(const thread_state_cb_t& cb) {
        std::vector< uint8_t > tnums;
        tnums.reserve(m_free_thread_slots.size());
        {
            std::unique_lock lock(m_init_mutex);
            m_registered_notifiers.push_back(cb);

            // We need to make a callback to this registeree with all running threads
            auto running_thread_slots = ~m_free_thread_slots;
            auto tnum = running_thread_slots.find_first();
            while (tnum != INVALID_CURSOR) {
                tnums.push_back(tnum);
                tnum = running_thread_slots.find_next(tnum);
            }
        }

        for (auto tnum : tnums) {
            cb(tnum, thread_life_cycle::THREAD_ATTACHED);
        }
    }

    bool is_thread_running(uint32_t thread_num) {
        std::shared_lock lock(m_init_mutex);
        return !m_free_thread_slots[thread_num];
    }

#if 0
    void register_buf(const std::function<void(uint32_t)> &func) {
        std::unique_lock lock(m_init_mutex);
        m_registered_bufs.push_back(func);

        // This callback needs to be called for existing running threads as well.
        auto tnum = m_refed_slots.find_first();
        while (tnum != INVALID_CURSOR) {
            bool thread_exited = m_free_thread_slots.test(tnum);
            if (!thread_exited) func(tnum);
            tnum = m_refed_slots.find_next(tnum);
        }
    }

    void for_all(std::function<bool(uint32_t, bool)> cb, bool locked_already = false) {
        std::vector< uint32_t > can_free_threads;
        {
            std::shared_lock lock(m_init_mutex);
            auto i = m_refed_slots.find_first();
            while (i != INVALID_CURSOR) {
                bool thread_exited = m_free_thread_slots.test(i);
                bool can_free = cb((uint32_t)i, thread_exited);

                // After callback, if the original thread is indeed freed, free up the slot as well.
                if (thread_exited && can_free) {
                    can_free_threads.push_back(i);
                }
                i = m_refed_slots.find_next(i);
            }
        }

        if (can_free_threads.size()) {
            std::unique_lock lock(m_init_mutex);
            for (auto i : can_free_threads) { slot_release(i); }
        }
    }

    bool for_specific(uint32_t thread_num, std::function<void(bool)> cb) {
        bool cb_called = false;
        std::unique_lock lock(m_init_mutex);

        if (m_busy_buf_slots[thread_num]) {
            bool thread_exited = m_free_thread_slots.test(i);
            cb(thread_exited);
            cb_called = true;
        }
        return cb_called;
    }
#endif

    static ThreadRegistry* instance() { return &inst; }
    static ThreadRegistry  inst;

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

#if 0
    void do_dec_buf(uint32_t thread_num) {
        m_ref_count[thread_num]--;
        if (m_ref_count[thread_num] == 0) { m_busy_buf_slots.reset(thread_num); }
    }
#endif

private:
    std::shared_mutex m_init_mutex;

    // A bitset where 1 marks for free thread slot, 0 for not free
    boost::dynamic_bitset<> m_free_thread_slots;

    // Next thread free slot
    boost::dynamic_bitset<>::size_type m_slot_cursor;

    // Number of buffers that are open for a given thread
    // boost::dynamic_bitset<> m_refed_slots;
    std::vector< sisl::atomic_counter< int > > m_ref_count;

    std::vector< thread_state_cb_t > m_registered_notifiers;
};

#define thread_registry ThreadRegistry::instance()

class ThreadLocalContext {
public:
    ThreadLocalContext() {
        this_thread_num = thread_registry->attach();
        // printf("Created new ThreadLocalContext with thread_num = %u\n", this_thread_num);
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

    uint32_t                  this_thread_num;
    std::array< uint64_t, 5 > user_contexts; // To store any user contexts
};

#define THREAD_BUFFER_INIT                                                                                             \
    sisl::ThreadRegistry                  sisl::ThreadRegistry::inst;                                                  \
    thread_local sisl::ThreadLocalContext sisl::ThreadLocalContext::inst;

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
        thread_registry->register_for_sc_notification(
            std::bind(&ThreadBuffer::on_thread_state_change, this, std::placeholders::_1, std::placeholders::_2));
    }

    T* get() {
        auto tnum = ThreadLocalContext::my_thread_num();
        assert(m_buffers[tnum].get() != nullptr);
        return m_buffers[tnum].get();
    }

    T& operator*() { return *(get()); }

    T* operator->() { return get(); }

    T* operator[](uint32_t n) {
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
                m_thread_slots.reset(thread_num);
                thread_registry->slot_release(thread_num);
            }
            break;
        }
    }

    uint32_t get_count() { return m_buffers.size(); }

    void access_all_threads(std::function< bool(T*, bool) > cb) {
        std::vector< uint32_t > can_free_thread_bufs;
        {
            std::shared_lock l(m_expand_mutex);
            auto             tnum = m_thread_slots.find_first();
            while (tnum != INVALID_CURSOR) {
                auto is_running = IsActiveThreadsOnly || thread_registry->is_thread_running(tnum);
                bool can_free = cb(m_buffers.at(tnum).get(), is_running) && !is_running;
                if (can_free) {
                    can_free_thread_bufs.push_back(tnum);
                }
                tnum = m_thread_slots.find_next(tnum);
            }
        }

        // We have some threads which have exited and caller allowed to free this buffer, free the slot
        // and reduce the ref count in the registry
        if (can_free_thread_bufs.size()) {
            std::unique_lock l(m_expand_mutex);
            for (auto i : can_free_thread_bufs) {
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
            if (!m_thread_slots[thread_num]) {
                return false;
            }

            auto is_running = IsActiveThreadsOnly || thread_registry->is_thread_running(thread_num);
            can_free = cb(m_buffers.at(thread_num).get(), is_running) && !is_running;
        }

        if (can_free) {
            std::unique_lock l(m_expand_mutex);
            m_thread_slots.reset(thread_num);
            thread_registry->slot_release(thread_num);
        }

        return true;
    }

#if 0
    // This method access the buffer for all the threads and do a callback with that thread.
    void access_all_threads(std::function<bool(T*, bool)> cb) {
        thread_registry->for_all(
            [this, cb](uint32_t thread_num, bool is_thread_exited) {
                bool can_free = true;
	            if (m_buffers[thread_num]) {
	                can_free = cb(m_buffers.at(thread_num).get(), is_thread_exited);
	            }
	            return gathered;
            });
    }

    bool access_specific_thread(uint32_t thread_num, std::function<void(T*, bool)> cb) {
        auto active = thread_registry->for_specific(thread_num,
                [this, cb](bool is_thread_exited) {
                    if (m_buffers[thread_num]) {
                        cb(m_buffers.at(thread_num).get(), is_thread_exited);
                    }
                });
        return active;
    }
#endif

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
    std::tuple< Args... >                       m_args;
    std::shared_mutex                           m_expand_mutex;
    boost::dynamic_bitset<>                     m_thread_slots;
};

template < typename T, typename... Args >
using ExitSafeThreadBuffer = ThreadBuffer< false, T, Args... >;

template < typename T, typename... Args >
class ActiveOnlyThreadBuffer : public ThreadBuffer< true, T, Args... > {
public:
    ActiveOnlyThreadBuffer(Args&&... args) : ThreadBuffer< true, T, Args... >(std::forward< Args >(args)...) {}

    void access_all_threads(std::function< void(T*) > cb) {
        ThreadBuffer< true, T, Args... >::access_all_threads([this, cb](T* t, bool is_thread_running) {
            assert(is_thread_running);
            cb(t);
            return false;
        });
    }

    bool access_specific_thread(uint32_t thread_num, std::function< void(T*) > cb) {
        return ThreadBuffer< true, T, Args... >::access_specific_thread(thread_num,
                                                                        [this, cb](T* t, bool is_thread_running) {
                                                                            assert(is_thread_running);
                                                                            cb(t);
                                                                            return false;
                                                                        });
    }
};

} // namespace sisl
