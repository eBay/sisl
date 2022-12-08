/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
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
 *
 *********************************************************************************/
#include <mutex>
#include <functional>
#include <vector>
#include "sisl/fds/vector_pool.hpp"
#include <boost/tti/has_member_function.hpp>

// Generate the metafunction
BOOST_TTI_HAS_MEMBER_FUNCTION(try_lock_shared)
BOOST_TTI_HAS_MEMBER_FUNCTION(unlock_shared)

template < typename T >
static constexpr bool try_lock_shared_check = has_member_function_try_lock_shared< T, bool >::value;

template < typename T >
static constexpr bool unlock_shared_check = has_member_function_unlock_shared< T, void >::value;

namespace sisl {
using post_lock_cb_t = std::function< void(void) >;

class _cb_wait_q {
public:
    _cb_wait_q() = default;
    ~_cb_wait_q() = default;

    void add_cb(const post_lock_cb_t& cb) {
        std::unique_lock< std::mutex > l(m_waitq_mutex);
        if (m_wait_q == nullptr) { m_wait_q = sisl::VectorPool< post_lock_cb_t >::alloc(); }
        m_wait_q->emplace_back(std::move(cb));
    }

    bool drain_cb() {
        std::vector< post_lock_cb_t >* wait_q{nullptr};
        {
            std::unique_lock< std::mutex > l(m_waitq_mutex);
            std::swap(wait_q, m_wait_q);
        }

        if (wait_q) {
            for (auto& cb : *wait_q) {
                cb();
            }
            sisl::VectorPool< post_lock_cb_t >::free(wait_q);
        }
        return (wait_q != nullptr);
    }

private:
    std::mutex m_waitq_mutex;
    std::vector< post_lock_cb_t >* m_wait_q{nullptr};
};

template < typename MutexImpl >
class CallbackMutex {
public:
    explicit CallbackMutex() = default;
    ~CallbackMutex() = default;

    bool try_lock(const post_lock_cb_t& cb) {
        if (m_base_mutex.try_lock()) {
            cb();
            return true;
        }
        m_q.add_cb(std::move(cb));
        return false;
    }

    template < class I = MutexImpl >
    typename std::enable_if< try_lock_shared_check< I >, bool >::type try_lock_shared(const post_lock_cb_t& cb) {
        if (m_base_mutex.try_lock_shared()) {
            cb();
            return true;
        }
        m_q.add_cb(std::move(cb));
        return false;
    }

    bool unlock() {
        auto drained = m_q.drain_cb();
        m_base_mutex.unlock();
        return drained;
    }

    template < class I = MutexImpl >
    typename std::enable_if< unlock_shared_check< I >, void >::type unlock_shared() {
        m_base_mutex.unlock_shared();
    }

    template < class I = MutexImpl >
    typename std::enable_if< !unlock_shared_check< I >, void >::type unlock_shared() {
        m_base_mutex.unlock();
    }

    static constexpr bool shared_mode_supported = try_lock_shared_check< MutexImpl >;
#if 0
    template < class I = MutexImpl >
    static constexpr typename std::enable_if< try_lock_shared_check< I >, bool >::type shared_mode_supported() {
        return true;
    }

    template < class I = MutexImpl >
    static constexpr typename std::enable_if< !try_lock_shared_check< I >, bool >::type shared_mode_supported() {
        return false;
    }
#endif

private:
    MutexImpl m_base_mutex;
    _cb_wait_q m_q;
};

template < typename MutexImpl >
class CBUniqueLock {
public:
    CBUniqueLock(CallbackMutex< MutexImpl >& cb_mtx, const post_lock_cb_t& cb) : m_cb_mtx{cb_mtx} {
        m_locked = m_cb_mtx.try_lock(cb);
    }

    ~CBUniqueLock() {
        if (m_locked) { m_cb_mtx.unlock(); }
    }

private:
    CallbackMutex< MutexImpl >& m_cb_mtx;
    bool m_locked{false};
};

template < typename MutexImpl >
class CBSharedLock {
public:
    CBSharedLock(CallbackMutex< MutexImpl >& cb_mtx, const post_lock_cb_t& cb) : m_cb_mtx{cb_mtx} {
        m_locked = m_cb_mtx.try_lock_shared(cb);
    }

    ~CBSharedLock() {
        if (m_locked) { m_cb_mtx.unlock_shared(); }
    }

private:
    CallbackMutex< MutexImpl >& m_cb_mtx;
    bool m_locked{false};
};

} // namespace sisl
