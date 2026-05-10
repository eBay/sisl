#pragma once

#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

namespace sisl::async {

// Virtual-time scheduler for unit tests. Holds a monotonic ns counter that
// only advances via explicit step() calls. schedule_at(ns) returns a stdexec
// sender that completes when virtual time crosses the requested wall_ns,
// which makes time-driven workloads (Poisson emitters, periodic
// checkpointers, drain timeouts) deterministic in tests without sleeping
// or running the io_uring CQE loop.
//
// Single-threaded by design: the test thread calls step() and the senders'
// completions fire on that same thread inside step()'s drain pass.
//
// Pattern (test-side):
//
//     sisl::async::manual_scheduler sched;
//     bool fired = false;
//     auto runner = [&]() -> exec::task<void> {
//         co_await sched.schedule_at(50'000'000);  // 50 ms virtual
//         fired = true;
//     };
//     exec::async_scope scope;
//     scope.spawn(runner());
//     EXPECT_FALSE(fired);
//     sched.step(std::chrono::milliseconds{49});
//     EXPECT_FALSE(fired);                          // not yet
//     sched.step(std::chrono::milliseconds{1});
//     EXPECT_TRUE(fired);                           // crossed 50 ms
class manual_scheduler {
public:
    manual_scheduler() = default;

    manual_scheduler(manual_scheduler const&)            = delete;
    manual_scheduler& operator=(manual_scheduler const&) = delete;

    // Current virtual time in nanoseconds.
    uint64_t now_ns() const noexcept { return _now; }

    // Advance virtual time by `delta`, then fire every timer whose
    // deadline is at or before the new time, in deadline order. Each
    // fired timer's set_value runs on this thread inline.
    template < typename Rep, typename Period >
    void step(std::chrono::duration< Rep, Period > delta) {
        _now += static_cast< uint64_t >(std::chrono::duration_cast< std::chrono::nanoseconds >(delta).count());
        drain_expired();
    }

    // Drain any timers whose deadline is already in the past without
    // advancing time. Useful when a test schedules a timer at "now" and
    // wants to confirm it fires immediately.
    void drain_expired() {
        while (!_timers.empty() && _timers.top().deadline <= _now) {
            entry const e = _timers.top();
            _timers.pop();
            e.fire(e.ctx);
        }
    }

    // ----- timer sender -----

    using fire_fn = void (*)(void*) noexcept;

    template < typename Receiver >
    struct timer_op {
        manual_scheduler* _sched;
        uint64_t          _deadline;
        Receiver          _receiver;

        void start() noexcept {
            if (_deadline <= _sched->_now) {
                stdexec::set_value(std::move(_receiver));
                return;
            }
            _sched->_timers.push(entry{_deadline, &timer_op::fire_thunk, this});
        }

        static void fire_thunk(void* p) noexcept {
            auto* const self = static_cast< timer_op* >(p);
            stdexec::set_value(std::move(self->_receiver));
        }
    };

    struct timer_sender {
        using sender_concept        = stdexec::sender_t;
        using completion_signatures = stdexec::completion_signatures< stdexec::set_value_t() >;

        manual_scheduler* _sched;
        uint64_t          _deadline;

        template < typename Receiver >
        timer_op< std::decay_t< Receiver > > connect(Receiver&& r) const noexcept {
            return {_sched, _deadline, std::forward< Receiver >(r)};
        }
    };

    [[nodiscard]] timer_sender schedule_at(uint64_t wall_ns) noexcept { return {this, wall_ns}; }

private:
    struct entry {
        uint64_t deadline;
        fire_fn  fire;
        void*    ctx;
        // Reverse order so std::priority_queue (default max-heap) acts
        // like a min-heap on deadline.
        bool operator<(entry const& o) const noexcept { return deadline > o.deadline; }
    };

    std::priority_queue< entry, std::vector< entry > > _timers;
    uint64_t                                           _now{0};
};

} // namespace sisl::async
