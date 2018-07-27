/*
 * bounded_queue.hpp
 *
 *  Created on: 30-Jan-2017
 *      Author: hkadayam
 */

#ifndef SRC_LIBUTILS_FDS_LOCKFREE_BOUNDED_Q_HPP_
#define SRC_LIBUTILS_FDS_LOCKFREE_BOUNDED_Q_HPP_

#include <boost/lockfree/queue.hpp>
#include "semaphore.hpp"

namespace fds {
/* This class implements the lockfree queue upto the point where queue size is filled.
 * If there are no empty spots, it tries to spin for certain iterations, before goes to
 * sleep until one slot becomes empty. So it is a unbounded in terms of queue functionality,
 * but it is lockfree only upto the bound.
 */
template <typename T> class lockfree_boundq {
  public:
    lockfree_boundq(uint32_t queue_size) {
        // Size of the Q to work with
        m_q = new auto(queue_size);

        // Preallocate queueSize space
        m_postsem = new LightweightSemaphore(queue_size);

        // We will let poster signal count instead of initialCount
        m_grabsem = new LightweightSemaphore(0);
    }

    virtual ~lockfree_boundq() {
        delete (m_q);
        delete (m_postsem);
        delete (m_grabsem);
    }

    void push(const T& v) {
        // NOTE: This function only waits if queue does not have any room
        m_postsem->wait();
        bool ret = m_q->push(v);
        assert(ret == true);
        m_grabsem->signal();
    }

    bool try_push(const T& v) {
        if (m_postsem->tryWait()) {
            bool ret = m_q->push(v);
            assert(ret == true);
            m_grabsem->signal();
            return ret;
        } else {
            return false;
        }
    }

    void pop(T& v) {
        while (true) {
            m_grabsem->wait();
            if (m_q->pop(v)) { break; }
        }

        m_postsem->signal();
    }

    bool try_pop(T& v) {
        if (m_grabsem->tryWait()) {
            if (m_q->pop(v)) { return true; }
        }

        return false;
    }

  private:
    // The actual Q we are wrapping on top.
    boost::lockfree::queue<T, boost::lockfree::fixed_sized<true>>* m_q;

    // Semaphore to wait for posters.
    LightweightSemaphore* m_postsem;

    // Semaphore to wait for grabbers
    LightweightSemaphore* m_grabsem;
};
} // namespace fds
#endif /* SRC_LIBUTILS_FDS_LOCKFREE_BOUNDED_Q_HPP_ */
