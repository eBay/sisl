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

namespace sisl {

#define MAX_THREADS_FOR_BUFFER        1024

typedef std::function<void(uint32_t)> thread_attach_cb_t;

class ThreadRegistry {
#define MAX_RUNNING_THREADS 1024
#define INVALID_CURSOR boost::dynamic_bitset<>::npos

  public:
    ThreadRegistry() :
            m_free_thread_slots(MAX_RUNNING_THREADS),
            m_busy_buf_slots(MAX_RUNNING_THREADS),
            m_bufs_open(MAX_RUNNING_THREADS, 0) {
        // Mark all slots as free
        m_free_thread_slots.set();
        m_slot_cursor = INVALID_CURSOR;
    }

    uint32_t attach() {
        uint32_t thread_num;

        std::lock_guard <std::mutex> lock(m_init_mutex);

        {
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
        }

        //std::cout << "ThreadRegistry: Attaching thread " << thread_num << "\n";
        //printf("ThreadRegistry: Attaching thread %u\n", thread_num);
	    for (auto cb : m_registered_cbs) {
	        cb(thread_num);
	        inc_buf(thread_num);
	    }
        return thread_num;
    }

    void detach(uint32_t thread_num) {
        m_free_thread_slots.set(thread_num);
	    sisl::urcu_ctl::unregister_rcu();
    }

    void inc_buf(uint32_t thread_num) {
        //std::lock_guard<std::mutex> lock(m_init_mutex);
        assert(!m_free_thread_slots.test(thread_num));
        m_busy_buf_slots.set(thread_num);
        m_bufs_open[thread_num]++;
    }

    void dec_buf(uint32_t thread_num) {
        std::lock_guard<std::mutex> lock(m_init_mutex);
        do_dec_buf(thread_num);
    }

    void register_new_thread_cb(const std::function<void(uint32_t)> &func) {
        std::lock_guard <std::mutex> lock(m_init_mutex);
        m_registered_cbs.push_back(func);

        // This callback needs to be called for existing running threads as well.
        auto tnum = m_busy_buf_slots.find_first();
        while (tnum != INVALID_CURSOR) {
            bool thread_exited = m_free_thread_slots.test(tnum);
            if (!thread_exited) func(tnum);
            tnum = m_busy_buf_slots.find_next(tnum);
        }
    }

    void for_all(std::function<bool(uint32_t, bool)> cb) {
        m_init_mutex.lock();
        auto i = m_busy_buf_slots.find_first();
        while (i != INVALID_CURSOR) {
            bool thread_exited = m_free_thread_slots.test(i);
            m_init_mutex.unlock();

            bool gathered = cb((uint32_t)i, thread_exited);

            // After callback, if the original thread is indeed freed, free up the slot as well.
            m_init_mutex.lock();
            if (thread_exited && gathered) { do_dec_buf(i); }

            i = m_busy_buf_slots.find_next(i);
        }
        m_init_mutex.unlock();
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
        } while ((m_slot_cursor == INVALID_CURSOR) || (m_bufs_open[m_slot_cursor] > 0));

        return (uint32_t)m_slot_cursor;
    }

    void do_dec_buf(uint32_t thread_num) {
        m_bufs_open[thread_num]--;
        if (m_bufs_open[thread_num] == 0) { m_busy_buf_slots.reset(thread_num); }
    }

  private:
    std::mutex m_init_mutex;

    // A bitset where 1 marks for free thread slot, 0 for not free
    boost::dynamic_bitset<> m_free_thread_slots;

    // Next thread free slot
    boost::dynamic_bitset<>::size_type m_slot_cursor;

    // Number of buffers that are open for a given thread
    boost::dynamic_bitset<> m_busy_buf_slots;
    std::vector<int> m_bufs_open;

    std::vector<thread_attach_cb_t> m_registered_cbs;
};

#define thread_registry ThreadRegistry::instance()

class ThreadLocalContext {
  public:
    ThreadLocalContext() {
        this_thread_num = thread_registry->attach();
        //printf("Created new ThreadLocalContext with thread_num = %u\n", this_thread_num);
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
    std::array<uint64_t, 5> user_contexts; // To store any user contexts
};

#define THREAD_BUFFER_INIT                                                                                             \
    sisl::ThreadRegistry sisl::ThreadRegistry::inst;                                                                     \
    thread_local sisl::ThreadLocalContext sisl::ThreadLocalContext::inst;

template <typename T, typename... Args>
class ThreadBuffer {
  public:
    template <class... Args1>
    //ThreadBuffer(Args1&&... args) : m_args(std::make_tuple(std::forward<Args1>(args)...) {
    //ThreadBuffer(Args1&&... args) : m_args(std::forward_as_tuple((args)...)) {
    ThreadBuffer(Args1&&... args) :
            m_args(std::forward<Args1>(args)...) {
        m_buffers.reserve(MAX_THREADS_FOR_BUFFER);
        thread_registry->register_new_thread_cb(std::bind(&ThreadBuffer::on_new_thread, this, std::placeholders::_1));
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

    void on_new_thread(uint32_t thread_num) {
        std::lock_guard<std::mutex> guard(m_expand_mutex);
        create_buffer(thread_num, m_args, std::index_sequence_for<Args...>());
    }

    uint32_t get_count() { return m_buffers.size(); }

    // This method access the buffer for all the threads and do a callback with that thread.
    void access_all_threads(std::function<bool(T*)> cb) {
        thread_registry->for_all(
            [this, cb](uint32_t thread_num, bool is_thread_exited) {
                bool gathered = true;
                (void) is_thread_exited;
	            if (m_buffers[thread_num]) {
	                gathered = cb(m_buffers.at(thread_num).get());
	            }
	            return gathered;
            });
    }

    void reset() { m_buffers[ThreadLocalContext::my_thread_num()].reset(); }

  private:
    template<std::size_t... Is>
    void create_buffer(uint32_t tnum, const std::tuple<Args...>& tuple, std::index_sequence<Is...>) {
        m_buffers[tnum] = std::make_unique<T>(std::get<Is>(tuple)...);
    }

  private:
    sisl::fds::sparse_vector<std::unique_ptr<T>> m_buffers;
    std::tuple<Args...> m_args;
    std::mutex m_expand_mutex;
};

} // namespace sisl
