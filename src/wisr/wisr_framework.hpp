//
// Created by Kadayam, Hari on 12/21/18.
//
#ifndef SISL_FDS_WAITFREE_WRITE_DS_HPP
#define SISL_FDS_WAITFREE_WRITE_DS_HPP

#include "utility/thread_buffer.hpp"
#include "utility/urcu_helper.hpp"
#include <tuple>

namespace sisl {

template < typename DS, typename... Args >
class WrapperBuf {
public:
    template < class... Args1 >
    WrapperBuf(Args1&&... args) : m_safe_buf(std::forward< Args1 >(args)...), m_args(std::forward< Args1 >(args)...) {}

    ~WrapperBuf() {}

    sisl::urcu_ptr< DS >  get_safe() { return m_safe_buf.get(); }
    std::shared_ptr< DS > make_and_exchange() {
        // auto n = m_safe_buf.make_and_exchange(std::forward<Args>(m_args)...);
        // auto n = std::apply(m_safe_buf.make_and_exchange, m_args);
        return _make_and_exchange(m_args, std::index_sequence_for< Args... >());
    }

    std::unique_ptr< DS > create_buf() {
        // return std::make_unique< DS >(std::forward<Args>(m_args)...);
        // return std::apply(std::make_unique< DS >, m_args);
        return _create_buf(m_args, std::index_sequence_for< Args... >());
    }

    void make() { return _make(m_args, std::index_sequence_for< Args... >()); }

    std::shared_ptr< DS > exchange() { return m_safe_buf.exchange(); }

private:
    template < std::size_t... Is >
    std::unique_ptr< DS > _create_buf(const std::tuple< Args... >& tuple, std::index_sequence< Is... >) {
        return std::make_unique< DS >(std::get< Is >(tuple)...);
    }

    template < std::size_t... Is >
    std::shared_ptr< DS > _make_and_exchange(const std::tuple< Args... >& tuple, std::index_sequence< Is... >) {
        return m_safe_buf.make_and_exchange(std::get< Is >(tuple)...);
    }

    template < std::size_t... Is >
    void _make(const std::tuple< Args... >& tuple, std::index_sequence< Is... >) {
        return m_safe_buf.make(std::get< Is >(tuple)...);
    }

private:
    sisl::urcu_data< DS > m_safe_buf;
    std::tuple< Args... > m_args;
    bool                  m_is_batch_on;
};

/* This class implements a generic wait free writer framework to build a wait free writer structures on top of it.
 * The reader side is syncronized using locks and expected to perform very slow. However, writer side are wait free
 * using rcu and per thread buffer. Thus it can be typically used on structures which are very frequently updated but
 * rarely read (say metrics collection, list of garbage entries to cleanup etc)..
 */
template < typename DS, typename... Args >
class wisr_framework {
public:
    template < class... Args1 >
    wisr_framework(Args1&&... args) : m_buffer(std::forward< Args1 >(args)...) {
        m_base_obj = m_buffer->create_buf();
    }

    DS* insertable() { return m_buffer->get_safe().get(); }

    DS* now() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        _rotate();
        return m_base_obj.get();
    }

    DS* delayed() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        return m_base_obj.get();
    }

    std::unique_ptr< DS > get_copy_and_reset() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        _rotate();

        auto ret = std::move(m_base_obj);
        m_base_obj = m_buffer->create_buf();
        return ret;
    }

    /* This method simply makes new buffer and put it in old, however, it is not guaranteed that old buffers
     * are rotated or merged at this point. This is simply first step on getting latest copy.
     * Steps are
     * 1. Call prepare_rotate()
     * 2. When safe call urcu_ctl::sync_rcu()
     * 3. Call deferred()
     */
    void prepare_rotate() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        m_buffer.access_all_threads([](WrapperBuf< DS, Args... >* ptr, bool is_thread_running) {
            (void)is_thread_running;
            ptr->make();
            return false;
        });
    }

    DS* deferred() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        auto                          base_raw = m_base_obj.get();
        m_buffer.access_all_threads([base_raw](WrapperBuf< DS, Args... >* ptr, bool is_thread_running) {
            (void)is_thread_running;
            auto old_ptr = ptr->exchange();
            if (old_ptr) {
                DS::merge(base_raw, old_ptr.get());
            }
            return true;
        });
        return m_base_obj.get();
    }

private:
    // This method assumes that rotate mutex is already held
    void _rotate() {
        auto base_raw = m_base_obj.get();
        m_buffer.access_all_threads([base_raw](WrapperBuf< DS, Args... >* ptr, bool is_thread_running) {
            (void)is_thread_running;
            auto old_ptr = ptr->make_and_exchange();
            DS::merge(base_raw, old_ptr.get());
            return true;
        });
    }

private:
    sisl::ExitSafeThreadBuffer< WrapperBuf< DS, Args... >, Args... > m_buffer;
    std::mutex                                                       m_rotate_mutex;
    std::unique_ptr< DS >                                            m_base_obj;
};

} // namespace sisl

#endif // SISL_FDS_WAITFREE_WRITE_DS_HPP
