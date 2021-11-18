//
// Created by Kadayam, Hari on 07/16/20
//

#pragma once
#include <utility/urcu_helper.hpp>
#include "logging/logging.h"

namespace sisl {
template < typename StatusT >
class StatusFactory {
public:
    template < class... Args1 >
    StatusFactory(Args1&&... args) {
        m_cur_status = new StatusT(std::forward< Args1 >(args)...);
    }

    void readable(const auto& cb) const {
        rcu_read_lock();
        auto s = rcu_dereference(m_cur_status);
        cb((const StatusT*)s);
        rcu_read_unlock();
    }

    urcu_scoped_ptr< StatusT > access() const { return urcu_scoped_ptr< StatusT >(m_cur_status); }

    void updateable(const auto& edit_cb) {
        std::scoped_lock l(m_updater_mutex);
        StatusT* new_ptr = new StatusT((const StatusT&)*(access().get())); // Create new status from old status
        edit_cb(new_ptr);

        auto old_ptr = rcu_xchg_pointer(&m_cur_status, new_ptr);
        synchronize_rcu();
        delete old_ptr;
    }

private:
    /* RCU protected status data */
    StatusT* m_cur_status = nullptr;

    // Mutex to protect multiple updaters to run the copy step in parallel.
    std::mutex m_updater_mutex;
};
} // namespace sisl
