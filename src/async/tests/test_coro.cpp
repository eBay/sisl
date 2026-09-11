// Unit tests for sisl::async::coro.hpp -- the sync/detach bridges between non-coroutine code and
// sisl::async::task (exec::task). This header is not included anywhere else in the sisl repo, so nothing
// else forces the compiler to actually parse and instantiate it; these tests exist primarily to make sure
// it compiles cleanly, in addition to covering its documented behavior.

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include <sisl/async/coro.hpp>

namespace {

using sisl::async::shared_awaitable;
using sisl::async::task;
using sisl::async::value_awaitable;

// ============================================================================
// sync_get: blocking bridge, same-thread and cross-thread, value and void
// ============================================================================

TEST(coro, SyncGetReturnsImmediateValue) {
    auto v = sisl::async::sync_get([]() -> task< int > { co_return 5; }());
    EXPECT_EQ(v, 5);
}

TEST(coro, SyncGetHandlesVoidTask) {
    bool ran{false};
    sisl::async::sync_get([](bool* r) -> task< void > {
        *r = true;
        co_return;
    }(&ran));
    EXPECT_TRUE(ran);
}

TEST(coro, SyncGetRethrows) {
    auto thrower = []() -> task< int > {
        throw std::runtime_error("sync boom");
        co_return 0;
    };
    EXPECT_THROW((void)sisl::async::sync_get(thrower()), std::runtime_error);
}

TEST(coro, SyncGetCrossThreadCompletion) {
    value_awaitable< int > ev{};
    std::thread producer{[&ev] { ev.complete(123); }};
    auto v = sisl::async::sync_get([](value_awaitable< int >& e) -> task< int > { co_return co_await e; }(ev));
    producer.join();
    EXPECT_EQ(v, 123);
}

// ============================================================================
// await_shared / await_value / await_value_ref: wrap an awaitable into a task
// ============================================================================

TEST(coro, AwaitSharedResolvesToProducerValue) {
    auto aw = std::make_shared< shared_awaitable< int > >();
    std::thread producer{[aw] { aw->complete(42); }};
    auto v = sisl::async::sync_get(sisl::async::await_shared(aw));
    producer.join();
    EXPECT_EQ(v, 42);
}

TEST(coro, AwaitValueResolvesToProducerValue) {
    auto aw = std::make_shared< value_awaitable< int > >();
    std::thread producer{[aw] { aw->complete(43); }};
    auto v = sisl::async::sync_get(sisl::async::await_value(aw));
    producer.join();
    EXPECT_EQ(v, 43);
}

TEST(coro, AwaitValueRefResolvesToProducerValue) {
    value_awaitable< int > ev{};
    std::thread producer{[&ev] { ev.complete(44); }};
    auto v = sisl::async::sync_get(sisl::async::await_value_ref(ev));
    producer.join();
    EXPECT_EQ(v, 44);
}

// ============================================================================
// detach() / detach_then(): fire-and-forget, exception-swallowing
// ============================================================================

TEST(coro, DetachRunsTaskToCompletion) {
    value_awaitable< int > ev{};
    bool got{false};
    auto t = [](value_awaitable< int >& e, bool* g) -> task< int > {
        auto const v = co_await e;
        *g = true;
        co_return v;
    }(ev, &got);

    sisl::async::detach(std::move(t));
    EXPECT_FALSE(got); // suspended on ev; detach() only runs inline up to the first suspension
    ev.complete(9);
    EXPECT_TRUE(got);
}

TEST(coro, DetachSwallowsException) {
    auto t = []() -> task< int > {
        throw std::runtime_error("detached boom");
        co_return 0;
    }();
    EXPECT_NO_THROW(sisl::async::detach(std::move(t)));
}

TEST(coro, DetachThenInvokesCallbackWithResult) {
    value_awaitable< int > ev{};
    std::optional< int > seen{};
    auto t = [](value_awaitable< int >& e) -> task< int > { co_return co_await e; }(ev);

    sisl::async::detach_then(std::move(t), [&seen](int v) { seen = v; });
    EXPECT_FALSE(seen.has_value());
    ev.complete(21);
    ASSERT_TRUE(seen.has_value());
    EXPECT_EQ(*seen, 21);
}

// ============================================================================
// sync_wait_for: bounded blocking wait
// ============================================================================

TEST(coro, SyncWaitForReturnsTrueWhenCompletedInTime) {
    value_awaitable< int > ev{};
    std::thread producer{[&ev] { ev.complete(1); }};
    auto const completed = sisl::async::sync_wait_for(sisl::async::await_value_ref(ev), std::chrono::seconds(5));
    producer.join();
    EXPECT_TRUE(completed);
}

TEST(coro, SyncWaitForReturnsFalseOnTimeout) {
    value_awaitable< int > ev{};
    auto const completed = sisl::async::sync_wait_for(sisl::async::await_value_ref(ev), std::chrono::milliseconds(10));
    EXPECT_FALSE(completed);
    // The detached task started by sync_wait_for is still suspended on ev; ev is a stack local that the task
    // holds a reference to, so it must be completed before ev goes out of scope (documented caller obligation).
    ev.complete(0);
}

} // namespace

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
