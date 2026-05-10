// Self-test for sisl::async::manual_scheduler. Validates that timer
// senders fire only after virtual time crosses their deadline, in
// deadline order, and that step() drains all expired timers in one pass.

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include <exec/async_scope.hpp>
#include <exec/task.hpp>
#include <stdexec/execution.hpp>

#include <sisl/async/manual_scheduler.hpp>

namespace {

TEST(manual_scheduler, step_below_deadline_does_not_fire) {
    sisl::async::manual_scheduler sched;
    bool                          fired  = false;
    auto                          runner = [&]() -> exec::task< void > {
        co_await sched.schedule_at(50'000'000);
        fired = true;
    };
    exec::async_scope scope;
    scope.spawn(runner());

    sched.step(std::chrono::milliseconds{49});
    EXPECT_FALSE(fired);
    sched.step(std::chrono::milliseconds{1});
    EXPECT_TRUE(fired);
    stdexec::sync_wait(scope.on_empty());
}

TEST(manual_scheduler, step_past_deadline_fires_in_one_drain) {
    sisl::async::manual_scheduler sched;
    bool                          fired  = false;
    auto                          runner = [&]() -> exec::task< void > {
        co_await sched.schedule_at(50'000'000);
        fired = true;
    };
    exec::async_scope scope;
    scope.spawn(runner());

    sched.step(std::chrono::milliseconds{500});
    EXPECT_TRUE(fired);
    EXPECT_EQ(500'000'000u, sched.now_ns());
    stdexec::sync_wait(scope.on_empty());
}

TEST(manual_scheduler, multiple_timers_fire_in_deadline_order) {
    sisl::async::manual_scheduler sched;
    std::vector< int >            order;

    auto schedule_one = [&](int label, uint64_t wall_ns) -> exec::task< void > {
        co_await sched.schedule_at(wall_ns);
        order.push_back(label);
    };

    exec::async_scope scope;
    // Spawn out of deadline order on purpose; expect step to fire them
    // in deadline order regardless of spawn order.
    scope.spawn(schedule_one(/*label=*/3, 30'000'000));
    scope.spawn(schedule_one(/*label=*/1, 10'000'000));
    scope.spawn(schedule_one(/*label=*/2, 20'000'000));

    sched.step(std::chrono::milliseconds{5});
    EXPECT_TRUE(order.empty());

    sched.step(std::chrono::milliseconds{15}); // now=20ms
    ASSERT_EQ(2u, order.size());
    EXPECT_EQ(1, order[0]);
    EXPECT_EQ(2, order[1]);

    sched.step(std::chrono::milliseconds{20}); // now=40ms
    ASSERT_EQ(3u, order.size());
    EXPECT_EQ(3, order[2]);
    stdexec::sync_wait(scope.on_empty());
}

TEST(manual_scheduler, deadline_already_past_fires_immediately) {
    sisl::async::manual_scheduler sched;
    sched.step(std::chrono::milliseconds{100}); // now=100ms

    bool fired  = false;
    auto runner = [&]() -> exec::task< void > {
        // Schedule a deadline that is already in the past.
        co_await sched.schedule_at(50'000'000);
        fired = true;
    };
    exec::async_scope scope;
    scope.spawn(runner());

    // No step() call: timer should fire on connect since deadline <= _now.
    EXPECT_TRUE(fired);
    stdexec::sync_wait(scope.on_empty());
}

} // namespace
