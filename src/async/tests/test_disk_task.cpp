// Unit tests for sisl::async::disk_task<T> and hot_task<T>.
//
// Tests cover: laziness (initial suspend), symmetric transfer, co_return value,
// hot_task fan-out, and the full disk_task + cqe_awaitable round-trip as it
// would be used by a drive backend (submit sqe -> suspend -> resume on CQE).
// No io_uring or stdexec required -- completion is driven manually.

#include <coroutine>

#include <gtest/gtest.h>

#include <sisl/async/cqe_state.hpp>
#include <sisl/async/disk_task.hpp>

namespace {

// ============================================================================
// Helpers
// ============================================================================

// Eager, detached coroutine for test drivers.  Starts immediately and destroys
// its frame on completion (final_suspend = suspend_never).
struct eager_task {
    struct promise_type {
        eager_task get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };
};

// ============================================================================
// Laziness
// ============================================================================

TEST(disk_task, LazyDoesNotRunUntilAwaited) {
    bool ran = false;
    auto make_task = [&]() -> sisl::async::disk_task< int > {
        ran = true;
        co_return 0;
    };
    auto task = make_task();
    EXPECT_FALSE(ran); // initial_suspend stops it before the body runs
    // Resuming via start() drives it to first co_await / co_return
    std::move(task).start();
    EXPECT_TRUE(ran);
}

// ============================================================================
// co_return value via hot_task
// ============================================================================

TEST(disk_task, CoReturnValuePropagatedThroughHotTask) {
    auto task = []() -> sisl::async::disk_task< int > { co_return 42; }();
    auto hot = std::move(task).start();
    EXPECT_TRUE(hot.done());
    EXPECT_EQ(hot.result(), 42);
}

// ============================================================================
// Symmetric transfer: outer co_await inner
// ============================================================================

TEST(disk_task, SymmetricTransferBetweenTasks) {
    int result = 0;
    auto inner = []() -> sisl::async::disk_task< int > { co_return 99; };
    auto outer = [&]() -> eager_task { result = co_await inner(); };
    outer(); // starts immediately; symmetric transfer drives inner to completion
    EXPECT_EQ(result, 99);
}

// ============================================================================
// Full round-trip: disk_task + cqe_awaitable (simulating CQE delivery)
// ============================================================================

// Simulates a drive backend: allocates a cqe_awaitable in "iocb" memory,
// submits an "SQE", suspends, then an external call to complete_cqe_state
// delivers the result.
TEST(disk_task, FullRoundTripWithCqeAwaitable) {
    sisl::async::cqe_awaitable state{};

    // The coroutine stores its continuation in state and suspends.
    auto make_io_task = [&]() -> sisl::async::disk_task< int > {
        int res = co_await state; // suspends here, stores handle in state._waiter
        co_return res;
    };

    auto task = make_io_task().start(); // starts coroutine, reaches co_await, suspends
    EXPECT_FALSE(task.done());          // still waiting for the CQE
    EXPECT_TRUE(state._waiter);         // waiter stashed

    // Simulate CQE delivery: complete_cqe_state sets _result and resumes.
    sisl::async::complete_cqe_state(state, 128);

    EXPECT_TRUE(task.done());
    EXPECT_EQ(task.result(), 128);
}

// ============================================================================
// hot_task fan-out: submit multiple tasks, then await each
// ============================================================================

TEST(disk_task, HotTaskFanOut) {
    sisl::async::cqe_awaitable s1{}, s2{}, s3{};

    auto make_io = [](sisl::async::cqe_awaitable& s) -> sisl::async::disk_task< int > { co_return co_await s; };

    // Start all three — each submits (or in this test, just suspends) before we co_await any.
    auto h1 = make_io(s1).start();
    auto h2 = make_io(s2).start();
    auto h3 = make_io(s3).start();
    EXPECT_FALSE(h1.done());
    EXPECT_FALSE(h2.done());
    EXPECT_FALSE(h3.done());

    // Complete out-of-order to verify each waiter is independent.
    sisl::async::complete_cqe_state(s3, 30);
    sisl::async::complete_cqe_state(s1, 10);
    sisl::async::complete_cqe_state(s2, 20);

    EXPECT_TRUE(h1.done());
    EXPECT_TRUE(h2.done());
    EXPECT_TRUE(h3.done());
    EXPECT_EQ(h1.result(), 10);
    EXPECT_EQ(h2.result(), 20);
    EXPECT_EQ(h3.result(), 30);
}

// ============================================================================
// hot_task await_ready fast-path: already-done task skips await_suspend
// ============================================================================

TEST(disk_task, HotTaskAwaitReadyFastPathForSyncCompletion) {
    auto task = []() -> sisl::async::disk_task< int > { co_return 7; }();
    auto hot = std::move(task).start();
    EXPECT_TRUE(hot.await_ready()); // completed synchronously
    EXPECT_EQ(hot.await_resume(), 7);
}

} // namespace
