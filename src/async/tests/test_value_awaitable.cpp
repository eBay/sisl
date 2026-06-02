// Unit tests for sisl::async::value_awaitable<T> -- the cross-thread, value-carrying
// generalization of cqe_awaitable used to bridge callback-style async completions
// (gRPC unary responses, nuraft callback results) into a co_await.
//
// Mirrors test_cqe_awaitable.cpp: std::noop_coroutine() stands in for a suspended
// coroutine's handle (no real frame), and the protocol is driven through the public
// complete()/await_* API so both completion orderings are exercised deterministically.
// No stdexec dependency -- these compile unconditionally like test_disk_task.

#include <atomic>
#include <coroutine>
#include <expected>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <sisl/async/value_awaitable.hpp>

namespace {

using string_await = sisl::async::value_awaitable< std::string >;

// ----------------------------------------------------------------------------
// not-ready before completion
// ----------------------------------------------------------------------------

TEST(value_awaitable, AwaitReadyFalseWhenNotReady) {
    string_await state{};
    EXPECT_FALSE(state.await_ready());
    EXPECT_FALSE(state._waiter);
}

// ----------------------------------------------------------------------------
// synchronous fast path: completion BEFORE the consumer co_awaits
// ----------------------------------------------------------------------------

TEST(value_awaitable, CompleteThenAwaitReadyTrue) {
    string_await state{};
    state.complete("hello");
    EXPECT_TRUE(state.await_ready()); // fast path: no suspension needed
    EXPECT_EQ(state.await_resume(), "hello");
}

// ----------------------------------------------------------------------------
// suspend-then-complete: consumer suspends first, completer resumes it.
// ----------------------------------------------------------------------------

TEST(value_awaitable, SuspendThenCompleteResumesWaiter) {
    string_await state{};

    auto const h = std::noop_coroutine();
    EXPECT_TRUE(state.await_suspend(h)); // not yet completed -> stay suspended
    ASSERT_TRUE(state._waiter);

    state.complete("world"); // fires the resume of the stored waiter (noop)
    EXPECT_EQ(state.await_resume(), "world");
}

// ----------------------------------------------------------------------------
// complete-then-suspend (the cross-thread race the atomic handshake fixes):
// completion lands before await_suspend installs the waiter; the wakeup must not
// be lost -- await_suspend returns false so the coroutine resumes immediately.
// ----------------------------------------------------------------------------

TEST(value_awaitable, CompleteThenSuspendDoesNotSuspend) {
    string_await state{};

    state.complete("early"); // completer ran first

    auto const h = std::noop_coroutine();
    EXPECT_FALSE(state.await_suspend(h)); // do not suspend; result already available
    EXPECT_EQ(state.await_resume(), "early");
}

// ----------------------------------------------------------------------------
// value carriage: move-only payload survives the round trip
// ----------------------------------------------------------------------------

TEST(value_awaitable, MoveOnlyValueRoundTrips) {
    sisl::async::value_awaitable< std::unique_ptr< int > > state{};
    state.complete(std::make_unique< int >(123));
    ASSERT_TRUE(state.await_ready());
    auto p = state.await_resume();
    ASSERT_TRUE(p);
    EXPECT_EQ(*p, 123);
}

// ----------------------------------------------------------------------------
// expected-shaped payload (the GrpcResult shape: value or error)
// ----------------------------------------------------------------------------

TEST(value_awaitable, ExpectedErrorRoundTrips) {
    sisl::async::value_awaitable< std::expected< std::string, int > > state{};
    state.complete(std::unexpected(42));
    ASSERT_TRUE(state.await_ready());
    auto r = state.await_resume();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), 42);
}

// ----------------------------------------------------------------------------
// refcounted shared state: the producer object is destroyed (as client_loop does
// after handle_response) while the consumer-facing handle keeps the result alive.
// ----------------------------------------------------------------------------

TEST(value_awaitable, SharedStateSurvivesProducerDestruction) {
    auto state = std::make_shared< string_await >();

    // Producer side completes, then drops its ref (mimics client_loop `delete data`).
    {
        auto producer_ref = state;
        producer_ref->complete("persisted");
    }
    ASSERT_EQ(state.use_count(), 1); // only the consumer-facing ref remains

    EXPECT_TRUE(state->await_ready());
    EXPECT_EQ(state->await_resume(), "persisted");
}

// ----------------------------------------------------------------------------
// Cross-thread handshake: one thread installs the waiter (await_suspend) while
// another delivers the completion (complete). Exactly one resume happens and the
// value is observed without a data race. Run under TSAN to catch ordering
// regressions in the lock-free handshake.
// ----------------------------------------------------------------------------

TEST(value_awaitable, CrossThreadHandshakeIsRaceFree) {
    constexpr int kIters = 2000;
    for (int i = 0; i < kIters; ++i) {
        string_await state{};
        std::atomic< bool > go{false};
        std::thread completer([&] {
            while (!go.load(std::memory_order_acquire)) {}
            state.complete(std::to_string(i));
        });

        go.store(true, std::memory_order_release);
        // Consumer races the completer: install the waiter (noop stands in for a frame).
        (void)state.await_suspend(std::noop_coroutine());

        completer.join(); // join publishes the completer's writes to this thread
        EXPECT_EQ(state.await_resume(), std::to_string(i));
    }
}

} // namespace
