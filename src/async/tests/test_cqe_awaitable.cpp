// Unit tests for sisl::async::cqe_awaitable.
//
// Migrated from ublkpp's test_cqe_state.cpp (the awaitable-protocol tests
// that have no dependency on ublksrv). We use std::noop_coroutine() as a
// waiter stand-in rather than spinning up a real coroutine, which avoids
// ASAN / coroutine-frame lifetime interactions on macOS. The full co_await
// round-trip (suspend -> complete -> await_resume captures result) is covered
// by ublkpp's test_cqe_state.cpp integration tests.
//
// cqe_awaitable uses a lock-free "whoever runs second resumes" handshake so
// await_suspend (consumer thread) and complete_cqe_state (completer thread)
// are race-free. These tests drive readiness through the real
// complete_cqe_state API and exercise both orderings deterministically.

#include <atomic>
#include <coroutine>
#include <thread>

#include <gtest/gtest.h>

#include <sisl/async/cqe_state.hpp>

namespace {

// ============================================================================
// construction: callback installed up front, awaitable starts not-ready
// ============================================================================

TEST(cqe_awaitable, CallbackInstalledAtConstruction) {
    sisl::async::cqe_awaitable state{};
    EXPECT_NE(state._on_complete, nullptr);
    EXPECT_EQ(state._on_complete_ctx, &state);
    EXPECT_FALSE(state._waiter);
}

TEST(cqe_awaitable, AwaitReadyFalseWhenNotReady) {
    sisl::async::cqe_awaitable state{};
    EXPECT_FALSE(state.await_ready());
}

// ============================================================================
// synchronous fast path: completion BEFORE the consumer co_awaits
// ============================================================================

TEST(cqe_awaitable, AwaitReadyTrueAfterCompletion) {
    sisl::async::cqe_awaitable state{};
    sisl::async::complete_cqe_state(state, 42);
    EXPECT_TRUE(state.await_ready()); // fast path: no suspension needed
    EXPECT_EQ(state.await_resume(), 42);
}

TEST(cqe_awaitable, AwaitResumeReturnsResult) {
    sisl::async::cqe_awaitable state{};
    sisl::async::complete_cqe_state(state, 7);
    EXPECT_EQ(state._result, 7);
    EXPECT_TRUE(state._result_ready);
    EXPECT_EQ(state.await_resume(), 7);
}

// ============================================================================
// suspend-then-complete: consumer suspends first, completer resumes it.
// std::noop_coroutine() stands in for the suspended coroutine's handle.
// ============================================================================

TEST(cqe_awaitable, SuspendThenCompleteResumesWaiter) {
    sisl::async::cqe_awaitable state{};

    auto const h = std::noop_coroutine();
    // Not yet completed: await_suspend keeps the coroutine suspended (returns true).
    EXPECT_TRUE(state.await_suspend(h));
    ASSERT_TRUE(state._waiter);

    // Completion fires the installed callback, which resumes the stored waiter.
    sisl::async::complete_cqe_state(state, 9);
    EXPECT_EQ(state._result, 9);
    EXPECT_EQ(state.await_resume(), 9);
}

// ============================================================================
// complete-then-suspend (the cross-thread race that the atomic handshake
// fixes): completion lands before await_suspend installs the waiter. The
// awaitable must NOT lose the wakeup -- await_suspend returns false so the
// coroutine resumes immediately instead of suspending forever.
// ============================================================================

TEST(cqe_awaitable, CompleteThenSuspendDoesNotSuspend) {
    sisl::async::cqe_awaitable state{};

    sisl::async::complete_cqe_state(state, 5); // completer ran first

    auto const h = std::noop_coroutine();
    EXPECT_FALSE(state.await_suspend(h)); // do not suspend; result already available
    EXPECT_EQ(state.await_resume(), 5);
}

// ============================================================================
// Cross-thread handshake -- the off-reactor race that the atomic state fixes:
// one thread installs the waiter (await_suspend) while another delivers the
// completion (complete_cqe_state). Exactly one resume happens and the result is
// observed without a data race on the cqe_state fields. Run under TSAN to catch
// ordering regressions in the lock-free handshake.
// ============================================================================

TEST(cqe_awaitable, CrossThreadHandshakeIsRaceFree) {
    constexpr int kIters = 2000;
    for (int i = 0; i < kIters; ++i) {
        sisl::async::cqe_awaitable state{};
        std::atomic< bool > go{false};
        std::thread completer([&] {
            while (!go.load(std::memory_order_acquire)) {}
            sisl::async::complete_cqe_state(state, i);
        });

        go.store(true, std::memory_order_release);
        // Consumer side races the completer: install the waiter (noop stands in for a real frame).
        (void)state.await_suspend(std::noop_coroutine());

        completer.join(); // join publishes the completer's writes to this thread
        EXPECT_EQ(state.await_resume(), i);
    }
}

} // namespace
