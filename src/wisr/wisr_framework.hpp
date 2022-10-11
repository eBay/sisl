/************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 * Author/Developer(s): Harihara Kadayam
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
 **************************************************************************/
#pragma once

#include <memory>
#include <mutex>
#include <tuple>
#include <vector>

#include "../utility/thread_buffer.hpp"
#include "../utility/urcu_helper.hpp"

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

    void insertable(const auto& cb) {
        auto _access_ptr = m_buffer->access();
        cb(_access_ptr.get());
    }

    [[nodiscard]] auto insert_access() const { return m_buffer->access(); }

    DS* now() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        _rotate_all_thread_bufs(true /* do_merge */);
        return m_base_obj.get();
    }

    DS* delayed() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        return m_base_obj.get();
    }

    std::unique_ptr< DS > get_copy_and_reset() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        _rotate_all_thread_bufs(true /* do_merge */);

        auto ret = std::move(m_base_obj);
        m_base_obj = _create_buf(m_args, std::index_sequence_for< Args... >());
        return ret;
    }

    /*
     * This method gets the unmerged copy of all per thread data structure.
     * NOTE: The pointer of DS inside vector needs to be freed by the caller, otherwise there will be memory leak.
     */
    std::vector< DS* > get_unmerged_and_reset() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        return _rotate_all_thread_bufs(false /* do_merge */);
    }

    void reset() {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        m_buffer.access_all_threads([](sisl::urcu_scoped_ptr< DS, Args... >* const ptr,
                                       [[maybe_unused]] const bool is_thread_running,
                                       [[maybe_unused]] const bool is_last_thread) {
            auto old_ptr = ptr->make_and_exchange(false /* sync_rcu_now */);
            delete old_ptr;
            return true;
        });
    }

    void foreach_thread_member(const auto& cb) {
        std::lock_guard< std::mutex > lg(m_rotate_mutex);
        m_buffer.access_all_threads([&cb](sisl::urcu_scoped_ptr< DS, Args... >* const ptr,
                                          [[maybe_unused]] const bool is_thread_running,
                                          [[maybe_unused]] const bool is_last_thread) {
            ptr->read(cb);
            return false;
        });
    }

private:
    // This method assumes that rotate mutex is already held
    std::vector< DS* > _rotate_all_thread_bufs(bool do_merge) {
        auto base_raw = m_base_obj.get();
        std::vector< DS* > old_ptrs;
        old_ptrs.reserve(128);

        m_buffer.access_all_threads([&old_ptrs](sisl::urcu_scoped_ptr< DS, Args... >* const ptr,
                                                [[maybe_unused]] const bool is_thread_running,
                                                [[maybe_unused]] const bool is_last_thread) {
            auto old_ptr = ptr->make_and_exchange(false /* sync_rcu_now */);
            old_ptrs.push_back(old_ptr);
            return true;
        });

        synchronize_rcu();

        if (do_merge) {
            for (auto& old_ptr : old_ptrs) {
                DS::merge(base_raw, old_ptr);
                delete old_ptr;
            }
            old_ptrs.clear();
        }
        return old_ptrs;
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
