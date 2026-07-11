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

#include <exception>
#include <tuple>
#include <utility>

#include <exec/inline_scheduler.hpp>
#include <stdexec/execution.hpp>

#include <sisl/async/task.hpp>
#include <sisl/logging/logging.h>

namespace sisl::async {

// Block the calling thread until the task completes and return its value (void for task<void>). Do NOT call on
// an event-loop / reactor thread.
template < typename Task >
inline auto sync_get(Task&& task) {
    auto result = stdexec::sync_wait(std::forward< Task >(task)).value();
    if constexpr (std::tuple_size_v< decltype(result) > == 0) {
        return;
    } else {
        return std::get< 0 >(std::move(result));
    }
}

// Fire-and-forget a coroutine whose result we don't need. The task is taken by value (copied into the
// self-owning wrapper frame); the wrapper swallows exceptions so a throwing body can't reach start_detached's
// receiver (which would std::terminate) -- tasks normally complete errors-as-values, so this is a backstop.
// write_env injects an inline scheduler so the sticky-affinity exec::task can start without an enclosing
// scheduler (it resumes inline on whatever thread completes its awaited work) -- the same idiom as when_all.
template < typename T >
inline void detach(task< T > t) {
    auto wrapper = [](task< T > inner) -> task< void > {
        try {
            co_await std::move(inner);
        } catch (const std::exception& e) { LOGERROR("Detached task threw, swallowing: {}", e.what()); } catch (...) {
            LOGERROR("Detached task threw an unknown exception, swallowing");
        }
    }(std::move(t));
    stdexec::start_detached(
        stdexec::write_env(std::move(wrapper), stdexec::prop{stdexec::get_scheduler, exec::inline_scheduler{}}));
}

} // namespace sisl::async
