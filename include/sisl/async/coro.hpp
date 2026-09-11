#pragma once

// The two bridges between synchronous, non-coroutine code and the stdexec / sisl::async::task world:
//   - sync_get(task) : block the calling thread until the task completes and return its value (void for
//                      task<void>). The task is fulfilled by other threads; sync_wait drains a run_loop here.
//                      Safe only OFF an event-loop / reactor thread (a test main, a synchronous control path);
//                      blocking a reactor with it would park that loop.
//   - detach(task)   : fire-and-forget a coroutine from a non-coroutine context (a void callback, a timer).
//                      exec::task is lazy, so an un-awaited task never runs -- this starts it.
//
// Requires stdexec on the include path (same opt-in as <sisl/async/task.hpp>).

#include <chrono>
#include <exception>
#include <future>
#include <tuple>
#include <utility>

#include <stdexec/execution.hpp>

#include <sisl/async/shared_awaitable.hpp>
#include <sisl/async/task.hpp>
#include <sisl/async/value_awaitable.hpp>
#include <sisl/logging/logging.h>

namespace sisl::async {

// Return a task<T> that awaits a heap-held completion. Used by the "do a synchronous side effect, then return a
// task that awaits the eventual completion" pattern (e.g. CP trigger / open_log_store: the switchover or map
// insert must run at call time, only the wait is deferred). The shared_ptr is copied into the task frame so the
// completion outlives the await.
template < typename T >
inline task< T > await_shared(std::shared_ptr< shared_awaitable< T > > aw) {
    co_return co_await *aw;
}
template < typename T >
inline task< T > await_value(std::shared_ptr< value_awaitable< T > > aw) {
    co_return co_await *aw;
}

// Await a value_awaitable held by reference (e.g. a long-lived member of a heap object that outlives the await).
// The reference is bound into the returned task's frame; the awaitable must stay alive until this task completes.
template < typename T >
inline task< T > await_value_ref(value_awaitable< T >& aw) {
    co_return co_await aw;
}

// write_env injects an inline scheduler so the sticky-affinity exec::task can be started without an enclosing
// scheduler context (it resumes inline on whatever thread completes its awaited sender); start_detached owns the
// operation-state on the heap and frees it on completion. Same pattern as iomgr's io_launch.hpp and sisl's
// when_all.
template < typename Task >
inline void start_coro(Task&& t) {
    stdexec::start_detached(stdexec::write_env(std::forward< Task >(t),
                                               stdexec::prop{stdexec::get_scheduler, stdexec::inline_scheduler{}}));
}

// Block the calling thread until the task completes and return its value. For the infrequent control-plane and
// shutdown paths that are synchronous today (e.g. a forced CP flush awaited before proceeding). The task is
// fulfilled by other threads, so this drains its run_loop here without self-deadlocking a data path.
template < typename Task >
inline auto sync_get(Task&& task) {
    auto result = stdexec::sync_wait(std::forward< Task >(task)).value();
    // result is a tuple of the task's completion values; for task<void> it is empty (nothing to return).
    if constexpr (std::tuple_size_v< decltype(result) > == 0) {
        return;
    } else {
        return std::get< 0 >(std::move(result));
    }
}

// Block the calling thread until the task completes OR `timeout` elapses; returns true iff it completed in time.
// The task is started detached and signals a std::promise on completion, which we time-wait on via its future.
// On timeout the detached task remains pending -- it must keep alive whatever it awaits (e.g. by holding a strong
// ref into its frame); we simply stop waiting and leave it to complete (or leak) later. Used by the data-receive
// timeout path, which then inspects per-item readiness and remediates the stragglers.
template < typename Task >
inline bool sync_wait_for(Task&& t, std::chrono::milliseconds timeout) {
    auto done = std::make_shared< std::promise< void > >();
    auto fut = done->get_future();
    start_coro([](std::decay_t< Task > inner, std::shared_ptr< std::promise< void > > d) -> task< void > {
        try {
            co_await std::move(inner);
        } catch (...) {}
        d->set_value();
    }(std::forward< Task >(t), std::move(done)));
    return fut.wait_for(timeout) == std::future_status::ready;
}

// Take the task BY VALUE so it is copied into the self-owning coroutine frame; a captured-by-reference task
// would dangle once start_detached returns. Swallows exceptions so a throwing body cannot reach start_detached's
// receiver (which would std::terminate) -- tasks normally complete errors-as-values, so this is a backstop.
template < typename T >
inline task< void > detach_wrapper(task< T > t) {
    try {
        co_await std::move(t);
    } catch (const std::exception& e) { LOGERROR("Detached task threw, swallowing: {}", e.what()); } catch (...) {
        LOGERROR("Detached task threw an unknown exception, swallowing");
    }
}

// Fire-and-forget a task whose result is not needed (e.g. a non-forced CP trigger). Starts it detached.
template < typename T >
inline void detach(task< T > t) {
    start_coro(detach_wrapper< T >(std::move(t)));
}

// Fire-and-forget a task but invoke fn(result) when it completes (the non-blocking ".thenValue(cb)" shape).
// fn runs on whatever thread completes the task. Both task and fn are copied into the self-owning frame.
template < typename T, typename Fn >
inline void detach_then(task< T > t, Fn fn) {
    start_coro(
        [](task< T > inner, Fn f) -> task< void > { f(co_await std::move(inner)); }(std::move(t), std::move(fn)));
}

} // namespace sisl::async
