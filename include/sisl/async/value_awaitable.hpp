#pragma once

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <optional>
#include <utility>

namespace sisl::async {

// Cross-thread, single-shot awaitable carrying a value of type T.
//
// Generalizes cqe_awaitable (cqe_state.hpp), which carries an int cqe->res, to an
// arbitrary T -- for callback-style async completions whose result is a value
// rather than a kernel return code (a gRPC unary response, a nuraft callback
// result, etc.). The lock-free init/waiting/done handshake is identical: the
// producer (any thread) calls complete(value); the consumer co_awaits. Whoever
// runs second performs the single resume, restoring the cross-thread
// happens-before that a std::future's shared state used to provide.
//
// THREAD SAFETY: the producer (complete) and the consumer (await_suspend) may run
// on different threads. _result is published before the producer's release-exchange
// and read after the consumer's matching acquire, so the value handoff is
// race-free. The canonical example is a gRPC completion delivered on a worker
// thread while the awaiting coroutine suspended on a reactor thread.
//
// LIFETIME: NON-MOVABLE -- the producer holds this object's address. Embed it in a
// stable location and keep it alive until complete() has fired AND the consumer has
// resumed past await_resume. Mirroring std::future's refcounted shared state, the
// canonical pattern is to place it inside a std::shared_ptr held by BOTH the
// producer op object and the consumer-facing awaitable handle; the producer object
// can then be destroyed independently of the pending/just-resumed consumer.
//
// EXACTLY-ONCE: complete() must be called exactly once. await_resume moves the
// value out, so the awaitable is single-shot.
//
// EXCEPTION DISCIPLINE: complete() is noexcept and calls _waiter.resume(); an
// exception escaping the resumed frame therefore calls std::terminate (same
// contract as cqe_awaitable::on_complete_thunk). A producer running on a noexcept
// completion boundary must ensure the resumed coroutine body cannot throw across it.
template < typename T >
struct value_awaitable {
    enum : uint8_t { k_init = 0, k_waiting = 1, k_done = 2 };

    std::coroutine_handle<> _waiter{};
    std::atomic< uint8_t > _state{k_init};
    std::optional< T > _result{};

    value_awaitable() = default;
    value_awaitable(const value_awaitable&) = delete;
    value_awaitable& operator=(const value_awaitable&) = delete;

    // Producer side (any thread). Publishes the value, flips to done, and resumes
    // the consumer iff it already suspended. The acq_rel exchange publishes _result
    // across the thread boundary to a consumer that observes k_done.
    void complete(T value) noexcept {
        _result.emplace(std::move(value));
        if (_state.exchange(k_done, std::memory_order_acq_rel) == k_waiting) { _waiter.resume(); }
    }

    // Fast path: skip suspension if the completion already arrived (acquire makes
    // _result visible).
    bool await_ready() const noexcept { return _state.load(std::memory_order_acquire) == k_done; }

    // Returns true to stay suspended (the completer resumes us later); false to
    // resume immediately because the completion landed between await_ready and here.
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        _waiter = h;
        return _state.exchange(k_waiting, std::memory_order_acq_rel) != k_done;
    }

    T await_resume() noexcept { return std::move(*_result); }
};

} // namespace sisl::async
