// Unit tests for sisl::async::cqe_awaitable.
//
// Migrated from ublkpp's test_cqe_state.cpp (the awaitable-protocol tests
// that have no dependency on ublksrv). We use std::noop_coroutine() as a
// waiter stand-in rather than spinning up a real coroutine, which avoids
// ASAN / coroutine-frame lifetime interactions on macOS. The full co_await
// round-trip (suspend -> complete -> await_resume captures result) is covered
// by ublkpp's test_cqe_state.cpp integration tests.

#include <coroutine>

#include <gtest/gtest.h>

#include <sisl/async/cqe_state.hpp>

namespace {

// ============================================================================
// await_ready
// ============================================================================

TEST(cqe_awaitable, AwaitReadyFalseWhenNotReady) {
    sisl::async::cqe_awaitable state{};
    state._result_ready = false;
    EXPECT_FALSE(state.await_ready());
}

TEST(cqe_awaitable, AwaitReadyTrueWhenReady) {
    sisl::async::cqe_awaitable state{};
    state._result_ready = true;
    EXPECT_TRUE(state.await_ready());
}

// ============================================================================
// await_resume
// ============================================================================

TEST(cqe_awaitable, AwaitResumeReturnsResult) {
    sisl::async::cqe_awaitable state{};
    state._result = 42;
    state._result_ready = true;
    EXPECT_EQ(state.await_resume(), 42);
}

// ============================================================================
// await_suspend + complete_cqe_state
//
// std::noop_coroutine() stands in for the suspended coroutine's handle.
// complete_cqe_state fires _on_complete, which calls noop.resume() (a no-op)
// and exchanges _waiter to null. This verifies the full callback plumbing
// without requiring a live coroutine frame.
// ============================================================================

TEST(cqe_awaitable, AwaitSuspendWiresCallbackAndCompleteCqeStateResumesWaiter) {
    sisl::async::cqe_awaitable state{};
    EXPECT_FALSE(state._waiter);
    EXPECT_EQ(state._on_complete, nullptr);

    auto const h = std::noop_coroutine();
    state.await_suspend(h);

    ASSERT_TRUE(state._waiter);
    ASSERT_NE(state._on_complete, nullptr);
    EXPECT_EQ(state._on_complete_ctx, &state);

    sisl::async::complete_cqe_state(state, 7);

    EXPECT_EQ(state._result, 7);
    EXPECT_TRUE(state._result_ready);
    EXPECT_FALSE(state._waiter); // exchanged to null by the callback
}

} // namespace
