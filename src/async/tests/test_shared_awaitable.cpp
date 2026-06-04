// Unit tests for sisl::async::shared_awaitable<T> -- the multi-consumer (broadcast) generalization of
// value_awaitable used to replace folly::SharedPromise<T> (N callers awaiting one in-flight completion).
//
// Mirrors test_value_awaitable.cpp: std::noop_coroutine() stands in for suspended coroutine handles (no real
// frames), and the protocol is driven through the public complete()/await_* API so both completion orderings
// and the broadcast (many waiters, one complete) are exercised deterministically. Header-only, stdexec-free.

#include <atomic>
#include <coroutine>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <sisl/async/shared_awaitable.hpp>

namespace {

using string_await = sisl::async::shared_awaitable< std::string >;

// not-ready before completion
TEST(shared_awaitable, AwaitReadyFalseWhenNotReady) {
    string_await state{};
    EXPECT_FALSE(state.await_ready());
    EXPECT_FALSE(state.is_ready());
}

// synchronous fast path: completion BEFORE the consumer co_awaits
TEST(shared_awaitable, CompleteThenAwaitReadyTrue) {
    string_await state{};
    state.complete("hello");
    EXPECT_TRUE(state.await_ready());
    EXPECT_EQ(state.await_resume(), "hello");
}

// suspend-then-complete: consumer suspends first, completer resumes it
TEST(shared_awaitable, SuspendThenCompleteResumesWaiter) {
    string_await state{};
    auto const h = std::noop_coroutine();
    EXPECT_TRUE(state.await_suspend(h)); // not yet completed -> stay suspended
    state.complete("world");
    EXPECT_EQ(state.await_resume(), "world");
}

// complete-then-suspend race: completion lands before await_suspend installs the waiter; no lost wakeup.
TEST(shared_awaitable, CompleteThenSuspendDoesNotSuspend) {
    string_await state{};
    state.complete("early");
    auto const h = std::noop_coroutine();
    EXPECT_FALSE(state.await_suspend(h)); // do not suspend; result already available
    EXPECT_EQ(state.await_resume(), "early");
}

// broadcast: MANY waiters installed before completion are ALL resumed, each observing a copy.
TEST(shared_awaitable, BroadcastResumesAllWaiters) {
    sisl::async::shared_awaitable< int > state{};

    // A handful of distinct suspended coroutines (noop handles all compare/resume fine).
    constexpr int kWaiters = 8;
    for (int i = 0; i < kWaiters; ++i) {
        EXPECT_TRUE(state.await_suspend(std::noop_coroutine()));
    }
    state.complete(77); // single completion fans out to all installed waiters

    // Every (late) observation returns the same broadcast value -- the result was copied, not moved out.
    for (int i = 0; i < kWaiters; ++i) {
        EXPECT_EQ(state.await_resume(), 77);
    }
}

// second complete() is a no-op (the broadcast already fired)
TEST(shared_awaitable, SecondCompleteIsIgnored) {
    sisl::async::shared_awaitable< int > state{};
    state.complete(1);
    state.complete(2); // ignored
    EXPECT_TRUE(state.await_ready());
    EXPECT_EQ(state.await_resume(), 1);
}

// copyable payload broadcasts to multiple late observers (the shared_ptr<HomeLogStore> shape)
TEST(shared_awaitable, CopyablePayloadBroadcasts) {
    sisl::async::shared_awaitable< std::shared_ptr< int > > state{};
    state.complete(std::make_shared< int >(123));
    auto a = state.await_resume();
    auto b = state.await_resume();
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_EQ(a.get(), b.get()); // same underlying object, shared
    EXPECT_EQ(*a, 123);
}

// refcounted shared state: the producer drops its ref after completing while a consumer ref keeps it readable.
TEST(shared_awaitable, SharedStateSurvivesProducerDestruction) {
    auto state = std::make_shared< string_await >();
    {
        auto producer_ref = state;
        producer_ref->complete("persisted");
    }
    ASSERT_EQ(state.use_count(), 1);
    EXPECT_TRUE(state->await_ready());
    EXPECT_EQ(state->await_resume(), "persisted");
}

// Cross-thread broadcast: several threads install waiters while one delivers the completion. Every waiter is
// resumed exactly once and observes the value without a data race. Run under TSAN to catch ordering regressions.
TEST(shared_awaitable, CrossThreadBroadcastIsRaceFree) {
    constexpr int kIters = 1000;
    for (int i = 0; i < kIters; ++i) {
        sisl::async::shared_awaitable< int > state{};
        std::atomic< bool > go{false};

        std::vector< std::thread > waiters;
        for (int w = 0; w < 4; ++w) {
            waiters.emplace_back([&] {
                while (!go.load(std::memory_order_acquire)) {}
                (void)state.await_suspend(std::noop_coroutine());
            });
        }
        std::thread completer([&] {
            while (!go.load(std::memory_order_acquire)) {}
            state.complete(i);
        });

        go.store(true, std::memory_order_release);
        for (auto& t : waiters) {
            t.join();
        }
        completer.join();
        EXPECT_EQ(state.await_resume(), i);
    }
}

} // namespace
