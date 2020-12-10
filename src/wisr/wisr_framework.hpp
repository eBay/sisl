//
// Created by Kadayam, Hari on 12/21/18.
//
#ifndef SISL_FDS_WAITFREE_WRITE_DS_HPP
#define SISL_FDS_WAITFREE_WRITE_DS_HPP

#include <memory>
#include <mutex>
#include <tuple>
#include <vector>

#include "utility/thread_buffer.hpp"
#include "utility/urcu_helper.hpp"

namespace sisl {

/* This class implements a generic wait free writer framework to build a wait free writer structures on top of it.
 * The reader side is synchronized using locks and expected to perform slower. However, writer side are wait free
 * using rcu and per thread buffer. Thus it can be typically used on structures which are very frequently updated but
 * rarely read (say metrics collection, list of garbage entries to cleanup etc)..
 */
template < typename DS, typename... Args >
class wisr_framework {
public:
    template < class... Args1 >
    wisr_framework(Args1&&... args) : m_buffer(std::forward< Args1 >(args)...), m_args(std::forward< Args1 >(args)...) {
        m_base_obj = std::make_unique< DS >(std::forward< Args1 >(args)...);
    }
    wisr_framework(const wisr_framework&) = delete;
    wisr_framework(wisr_framework&&) noexcept = delete;
    wisr_framework& operator=(const wisr_framework&) = delete;
    wisr_framework& operator=(wisr_framework&&) noexcept = delete;
    ~wisr_framework() = default;

    DS* insertable_ptr() { return m_buffer->access().get(); }

    void insertable(const auto& cb) {
        auto _access_ptr = m_buffer->access();
        cb(_access_ptr.get());
    }

    DS* now() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        _rotate_all_thread_bufs();
        return m_base_obj.get();
    }

    DS* delayed() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        return m_base_obj.get();
    }

    std::unique_ptr< DS > get_copy_and_reset() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        _rotate_all_thread_bufs();

        auto ret = std::move(m_base_obj);
        m_base_obj = _create_buf(m_args, std::index_sequence_for< Args... >());
        return ret;
    }

private:
    // This method assumes that rotate mutex is already held
    void _rotate_all_thread_bufs() {
        auto base_raw = m_base_obj.get();
        std::vector< DS* > old_ptrs;
        old_ptrs.reserve(128);

        m_buffer.access_all_threads([base_raw, &old_ptrs](sisl::urcu_scoped_ptr< DS, Args... >* const ptr,
                                                          [[maybe_unused]] const bool is_thread_running,
                                                          [[maybe_unused]] const bool is_last_thread) {
            auto old_ptr = ptr->make_and_exchange(false /* sync_rcu_now */);
            old_ptrs.push_back(old_ptr);
            return true;
        });

        synchronize_rcu();

        for (auto& old_ptr : old_ptrs) {
            DS::merge(base_raw, old_ptr);
            delete old_ptr;
        }
    }

    template < std::size_t... Is >
    std::unique_ptr< DS > _create_buf(const std::tuple< Args... >& tuple, std::index_sequence< Is... >) {
        return std::make_unique< DS >(std::get< Is >(tuple)...);
    }

private:
    sisl::ExitSafeThreadBuffer< sisl::urcu_scoped_ptr< DS, Args... >, Args... > m_buffer;
    std::mutex m_rotate_mutex;
    std::unique_ptr< DS > m_base_obj;
    std::tuple< Args... > m_args;
};

} // namespace sisl

#endif // SISL_FDS_WAITFREE_WRITE_DS_HPP
