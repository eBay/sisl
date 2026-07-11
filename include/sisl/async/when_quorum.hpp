#pragma once

// when_quorum: fan out over a runtime vector of tasks and resume as soon as `quorum` of them SUCCEED (complete
// with a value), leaving the stragglers running detached. The k-of-n sibling of sisl::async::when_all (same
// start_detached + value_awaitable latch), differing only in when the latch fires. The canonical use is
// quorum-acknowledged replication -- acknowledge at a majority instead of waiting for the slowest peer -- but
// the combinator is domain-agnostic.
//
// THE PAYLOAD CONTRACT. Children keep running after we resume, so a child must not reference a caller-owned
// buffer past its first suspension point -- by then the caller may have recycled it. Note the asymmetry that
// makes early return sound: the caller's buffer must survive every child's *send/first-suspension*, but we only
// wait for a quorum of *completions*. Every child runs synchronously to its first suspension inside the fan-out
// loop below, so all first suspensions are reached before we ever await the latch.
//
// THE RESULTS RACE, AND WHY IT IS NOT ONE. The latch fires on exactly two triggers:
//
//     (a) `quorum` children have succeeded  -- stragglers may still be writing into `results`
//     (b) every child has finished          -- nothing is writing into `results`
//
// So `results` is safe to read IF AND ONLY IF fewer than `quorum` successes came back: (a) cannot have fired,
// therefore (b) did. Do not read `results` without checking `acks < quorum` first.

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include <exec/inline_scheduler.hpp>
#include <stdexec/execution.hpp>

#include <sisl/async/task.hpp>
#include <sisl/async/value_awaitable.hpp>

namespace sisl::async {

namespace detail {

// Fires once, on whichever trigger lands first. `fired_` guards value_awaitable's exactly-once contract.
struct quorum_latch {
    std::atomic< std::size_t > remaining;
    std::atomic< std::size_t > acks{0};
    std::atomic< bool > fired{false};
    std::size_t const quorum;
    value_awaitable< std::monostate > done;

    quorum_latch(std::size_t n, std::size_t q) noexcept : remaining{n}, quorum{q} {}

    void arrive(bool acked) noexcept {
        // Order matters: publish the ack BEFORE the completion, so a waiter woken by the last child's
        // count_down observes every ack that preceded it.
        if (acked && (acks.fetch_add(1, std::memory_order_acq_rel) + 1 == quorum)) fire();
        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) fire();
    }
    void fire() noexcept {
        if (!fired.exchange(true, std::memory_order_acq_rel)) done.complete({});
    }
};

// Called once per child as it completes, on whatever thread completed it (stragglers included), with the
// child's fan-out index and whether it succeeded. Keyed on the local `acked` flag, NOT on results[index]: a
// throwing child leaves results[index] default-constructed, which for e.g. expected<void> reads has_value() ==
// true, a phantom success the hook must never publish.
using completion_hook = std::function< void(std::size_t index, bool acked) >;

// Awaits one child, records its result, and arrives at the latch. Swallows exceptions (a throwing child must
// not strand the latch), leaving that slot default-constructed and counted as a non-success.
template < typename T >
task< void > quorum_run_one(task< T > child, std::shared_ptr< std::vector< T > > results, std::size_t index,
                            std::shared_ptr< quorum_latch > latch, completion_hook on_each) {
    bool acked = false;
    try {
        auto r = co_await std::move(child);
        acked = r.has_value();
        (*results)[index] = std::move(r);
    } catch (...) {
        // leave results[index] default-constructed; counted as a non-success
    }
    // Publish evidence BEFORE arriving: on the quorum-th success, arrive() fires the latch and the awaiter
    // resumes, so a hook recording here is visible to that awaiter before it observes the result.
    if (on_each) on_each(index, acked);
    latch->arrive(acked);
}

} // namespace detail

template < typename T >
struct quorum_result {
    std::size_t acks{0};
    // Readable ONLY when acks < quorum (see the header comment): then every child has finished.
    std::shared_ptr< std::vector< T > > results;
};

// Start every task concurrently; resume once `quorum` of them have completed with a value, or once all have
// finished, whichever comes first. Stragglers keep running detached and keep `results` alive. `on_each`, if set,
// fires once per child as it completes (see completion_hook); default {} leaves behavior unchanged for callers
// that do not observe per-child outcomes.
template < typename T >
task< quorum_result< T > > when_quorum(std::vector< task< T > > tasks, std::size_t quorum,
                                       detail::completion_hook on_each = {}) {
    auto const n = tasks.size();
    auto results = std::make_shared< std::vector< T > >(n);
    if (n == 0) co_return quorum_result< T >{0, std::move(results)};

    auto latch = std::make_shared< detail::quorum_latch >(n, quorum);
    for (std::size_t i = 0; i < n; ++i) {
        // Each child runs synchronously to its first suspension right here, which is where it consumes the
        // caller's payload. exec::task has sticky scheduler affinity, so inject inline_scheduler to let the
        // detached child resume on whatever thread completes it (identical to sisl::async::when_all).
        stdexec::start_detached(
            stdexec::write_env(detail::quorum_run_one< T >(std::move(tasks[i]), results, i, latch, on_each),
                               stdexec::prop{stdexec::get_scheduler, exec::inline_scheduler{}}));
    }
    co_await latch->done;
    co_return quorum_result< T >{latch->acks.load(std::memory_order_acquire), std::move(results)};
}

} // namespace sisl::async
