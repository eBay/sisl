#pragma once

#include <coroutine>
#include <utility>

namespace sisl::async {

template < typename T >
struct hot_task;

// Lightweight lazy coroutine task for per-disk async I/O. Composable via symmetric transfer:
// co_await disk_task<T> inside an exec::task<void> or another disk_task<U> suspends the caller
// and immediately resumes the callee without going through any scheduler.
//
// Resumption is driven by whatever delivers completion events (e.g. io_uring CQE loop, AIO
// eventfd handler). When the submitter calls complete_cqe_state() or directly resumes the
// cqe_awaitable's _waiter, the chain propagates back to the caller via final_suspend symmetric
// transfer without any scheduler round-trip.
//
// Fan-out: call .start() on each child task before co_await-ing any of them. start() advances
// the child to its first suspension point (submitting the SQE) and returns a hot_task<T> that
// is directly co_awaitable. This lets the caller batch all SQE submissions before suspending.
//
// Lifetime: the coroutine frame is owned by the disk_task object. Move-only; the caller must
// co_await (consuming the task) or call .start() before the object is destroyed.
template < typename T >
struct disk_task {
    struct promise_type {
        T _value{};
        std::coroutine_handle<> _continuation{};

        disk_task get_return_object() noexcept {
            return disk_task{std::coroutine_handle< promise_type >::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }

        struct final_awaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle< promise_type > h) noexcept {
                auto cont = h.promise()._continuation;
                return cont ? cont : std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        final_awaiter final_suspend() noexcept { return {}; }

        void return_value(T v) noexcept { _value = v; }
        [[noreturn]] void unhandled_exception() { throw; }
    };

    std::coroutine_handle< promise_type > _coro;

    explicit disk_task(std::coroutine_handle< promise_type > h) noexcept : _coro(h) {}
    disk_task(disk_task&& o) noexcept : _coro(std::exchange(o._coro, {})) {}
    disk_task(disk_task const&) = delete;
    ~disk_task() {
        if (_coro) _coro.destroy();
    }

    // Awaitable interface: allows co_await disk_task<T> from exec::task<void> or disk_task<U>.
    // Uses symmetric transfer to start the callee without returning to the scheduler.
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont) noexcept {
        _coro.promise()._continuation = cont;
        return _coro; // symmetric transfer: start callee immediately
    }
    T await_resume() noexcept { return _coro.promise()._value; }

    // Starts the lazy coroutine and returns an owning awaitable for the result. Advances the
    // coroutine to its first suspension point (e.g. submitting an SQE), then returns control
    // to the caller. Fan-out: call start() on all children before co_await-ing any of them.
    hot_task< T > start() && noexcept;
};

// Owning, already-started task returned by disk_task<T>::start(). Used when the caller eagerly
// starts all child tasks (to submit SQEs in parallel) before suspending on results.
// await_ready() fast-paths tasks that completed synchronously before the co_await.
// Thread-safety: designed for single-threaded queue loops — no CQE can arrive between
// await_ready() and await_suspend() so no race on completion between the two calls.
template < typename T >
struct hot_task {
    disk_task< T > _task;

    bool await_ready() const noexcept { return _task._coro.done(); }
    void await_suspend(std::coroutine_handle<> cont) noexcept { _task._coro.promise()._continuation = cont; }
    T await_resume() const noexcept { return _task._coro.promise()._value; }

    bool done() const noexcept { return _task._coro.done(); }
    T result() const noexcept { return _task._coro.promise()._value; }
};

template < typename T >
hot_task< T > disk_task< T >::start() && noexcept {
    _coro.resume();
    return hot_task< T >{std::move(*this)};
}

} // namespace sisl::async
