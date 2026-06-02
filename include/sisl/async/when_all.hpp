#pragma once

// Dynamic (runtime-sized) fan-out for sisl::async::task<T>.
//
// stdexec's when_all is variadic (compile-time arity); the replication/data paths fan out over a
// runtime vector of peers (the collectAll/collectAllUnsafe sites). when_all() here starts every child
// task concurrently and completes once all have finished, collecting their results by index.
//
// Built on the tested value_awaitable cross-thread handshake (a counting latch) + start_detached with
// an inline scheduler -- the same pattern iomgr's drive launcher uses (io_launch.hpp): exec::task has
// sticky scheduler affinity, so write_env injects exec::inline_scheduler to start a detached task that
// resumes inline on whatever thread completes it.
//
// ERROR MODEL: errors-as-values. Each child task is expected to complete on its value channel with a T
// (e.g. a Result<...> that may hold an error). when_all does NOT short-circuit on a child error (unlike
// stdexec::when_all, which set_errors and cancels siblings) -- it always waits for every child, which is
// what the "errors per-peer intentionally ignored" broadcasts require. Should a child throw, that slot
// is left default-constructed (T must be default-constructible) and the fan-out still completes.
//
// THREADING: child continuations (and therefore the per-index result stores) run on whatever thread
// completes each task -- for the gRPC path, a GrpcAsyncClientWorker thread. The counting latch publishes
// a happens-before to the awaiting coroutine, so reading the result vector after the co_await is
// race-free. Distinct indices are written by distinct children, so there is no aliasing on the vector.

#include <atomic>
#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

#include <stdexec/execution.hpp>
#include <exec/inline_scheduler.hpp>

#include <sisl/async/task.hpp>
#include <sisl/async/value_awaitable.hpp>

namespace sisl::async {

namespace detail {

// N children count down once each; the awaitable fires when the last one does.
struct fan_latch {
    std::atomic< std::size_t > _count;
    value_awaitable< std::monostate > _done;

    explicit fan_latch(std::size_t n) noexcept : _count{n} {}

    void count_down() noexcept {
        if (_count.fetch_sub(1, std::memory_order_acq_rel) == 1) { _done.complete({}); }
    }
};

// Void wrapper coroutine (cf. iomgr's drive_wrapper): awaits one child, stores its result by index, and
// counts down. Swallows exceptions so the detached op never hits the set_error -> terminate path and so a
// single failing child cannot strand the latch.
template < typename T >
task< void > fan_run_one(task< T > child, std::shared_ptr< std::vector< T > > results, std::size_t index,
                         std::shared_ptr< fan_latch > latch) {
    try {
        (*results)[index] = co_await std::move(child);
    } catch (...) {
        // leave results[index] default-constructed
    }
    latch->count_down();
}

} // namespace detail

// Start every task concurrently; complete once all have finished, returning results in input order.
template < typename T >
task< std::vector< T > > when_all(std::vector< task< T > > tasks) {
    auto const n = tasks.size();
    auto results = std::make_shared< std::vector< T > >(n);
    if (n == 0) { co_return std::move(*results); }

    auto latch = std::make_shared< detail::fan_latch >(n);
    for (std::size_t i = 0; i < n; ++i) {
        stdexec::start_detached(stdexec::write_env(detail::fan_run_one< T >(std::move(tasks[i]), results, i, latch),
                                                   stdexec::prop{stdexec::get_scheduler, exec::inline_scheduler{}}));
    }
    co_await latch->_done;
    co_return std::move(*results);
}

} // namespace sisl::async
