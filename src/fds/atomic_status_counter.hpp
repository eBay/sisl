/*
 * atomic_status_counter.hpp
 *
 *  Created on: 12-Sep-2019
 *      Author: hkadayam
 */

#pragma once

#include <cassert>
#include <atomic>

namespace sisl {

template < typename StatusType, StatusType default_val >
struct _status_counter {
    int32_t counter = 0;
    StatusType status = default_val;

    _status_counter(int32_t cnt = 0, StatusType s = default_val) : counter(cnt), status(s) {}

    operator uint64_t() const { return *((uint64_t*)(this)); }
    uint64_t to_integer() const { return (uint64_t)(*this); }
} __attribute__((packed));

#define status_counter_t _status_counter< StatusType, default_val >

/*
   The class provides a atomic way to maintain counter and status or any 32bit entity. The max value
   of the counter is 2^31 and beyond which results are not deterministic.

   It does the atomicity by packing them in a word length boundary.
*/
template < typename StatusType, StatusType default_val >
struct atomic_status_counter {
    std::atomic< status_counter_t > m_val;

    /**
     * @brief Construct a new atomic status counter object
     *
     * @param counter: Counter value to set, defaults to 0
     * @param status Status value to set, defaults to default_val in template parameter
     */
    atomic_status_counter(int32_t counter = 0, StatusType status = default_val) :
            m_val(status_counter_t(counter, status)) {}

    /**
     * @brief Get the status portion of this container
     *
     * @return StatusType
     */
    StatusType get_status() const { return m_val.load(std::memory_order_acquire).status; }

    /**
     * @brief Get the value of the counter portion of this container
     *
     * @return int32_t
     */
    int32_t count() const { return m_val.load(std::memory_order_acquire).counter; }

    /**
     * @brief Update the status atomically without changing the current counter value
     *
     * @param status to set
     */
    void set_status(StatusType status) {
        set_value([status](status_counter_t& val) { val.status = status; });
    }

    /**
     * @brief Update the status atomically to new status only if current status is expected status
     *
     * @param exp_status
     * @param new_status
     */
    void xchng_status(StatusType exp_status, StatusType new_status) {
        set_value([exp_status, new_status](status_counter_t& val) {
            if (val.status == exp_status) { val.status = new_status; }
        });
    }

    /**
     * @brief First decrement the count by 1. In addition, if the count is now 0 and if the status is same as
     * expected status, then set the new status, all done atomically.
     *
     * @param exp_status
     * @param new_status
     * @return true or false based on if the counter reached 0 or not.
     */
    bool dec_xchng_status_ifz(StatusType exp_status, StatusType new_status) {
        auto new_val = set_value([exp_status, new_status](status_counter_t& val) {
            val.counter--;
            if ((val.counter == 0) && (val.status == exp_status)) { val.status = new_status; }
        });
        return (new_val.counter == 0);
    }

    /**
     * @brief If the status portion is expected, then increment the counter by 1
     *
     * @param exp_status
     * @return true or false based if it did change or not
     */
    bool increment_if_status(StatusType exp_status) {
        auto new_val = set_value([exp_status](status_counter_t& val) {
            if (val.status == exp_status) { val.counter++; }
        });
        return (new_val.status == exp_status);
    }

    /**
     * @brief Decrement the counter by 1, further check the status is same as expected and also counter has reached zero
     *
     * @param exp_status
     * @return true or false depending on ((status == exp_status) && (counter == 0)) done atomically
     */
    bool decrement_testz_and_test_status(StatusType exp_status) {
        auto new_val = set_value([exp_status](status_counter_t& val) { val.counter--; });
        return ((new_val.counter == 0) && (new_val.status == exp_status));
    }

    /**
     * @brief Decrement the counter portion by 1 and if the count reaches 0, then set the status to new status provided.
     * All these steps are done atomically.
     *
     * @param new_status
     * @return true or false depending on if counter reached 0 and that new status is set.
     */
    bool dec_set_status_ifz(StatusType new_status) {
        auto new_val = set_value([new_status](status_counter_t& val) {
            val.counter--;
            val.status = new_status;
        });
        return (new_val.counter == 0);
    }

    /**
     * @brief Increment the counter portion by the value specified in the counter
     *
     * @param count
     */
    void increment(int32_t count = 1) {
        set_value([count](status_counter_t& val) { val.counter += count; });
    }

    /**
     * @brief Decrement the counter portion by the value specified in the counter
     *
     * @param count
     */
    void decrement(int32_t count = 1) {
        set_value([count](status_counter_t& val) { val.counter -= count; });
    }

    /**
     * @brief Set the counter portion of the container with the value specified, without modifiying status portion.
     *
     * @param count
     */
    void set_counter(int32_t count) {
        set_value([count](status_counter_t& val) { val.counter = count; });
    }

    /**
     * @brief Decrement the counter portion of the container by the count and atomically check if the value is zero.
     *
     * @param count
     * @return true or false depending on if counter reached 0 or not.
     */
    bool decrement_testz(int32_t count = 1) {
        auto new_val = set_value([count](status_counter_t& val) { val.counter -= count; });
        return (new_val.counter == 0);
    }

    /**
     * @brief Gives the control to the caller through modifier callback which can change the value and pass it back
     * so it is updated atomically. The modifier has to pass a boolean flag to indicate if it indeed needs to change or
     * not.
     *
     * @param modifier: A callback which accepts counter and status pointers, whose values can be modified if needbe
     * @return true or false based on if the modifier has changed the value or not
     */
    bool set_atomic_value(const std::function< bool< int32_t&, StatusType& > >& modifier) {
        status_counter_t old_v, new_v;
        bool updated = true;
        do {
            old_v = m_val.load(std::memory_order_acquire);
            new_v = old_v;
            if (!modifier(new_v.counter, new_v.status)) {
                updated = false;
                break;
            }
        } while (!m_val.compare_exchange_weak(old_v, new_v, std::memory_order_acq_rel));

        return updated;
    }

private:
    status_counter_t set_value(const std::function< void(status_counter_t&) >& modifier) {
        status_counter_t old_v, new_v;
        do {
            old_v = m_val.load(std::memory_order_acquire);
            new_v = old_v;
            modifier(new_v);
        } while (!m_val.compare_exchange_weak(old_v, new_v, std::memory_order_acq_rel));

        return new_v;
    }
};
} // namespace sisl