#pragma once

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <utility>

namespace sisl::async {

// Bit 63 of cqe->user_data marks "this CQE belongs to a sisl::async-aware
// submitter that owns the user_data pointee." Canonical userspace addresses
// on x86_64/ARM64 use <=48 bits, so bit 63 is always zero in any valid
// pointer; the OR is safe and reversible.
//
// Sharing this contract across consumers (sisl's own callback cqe_state,
// billet's stdexec sender op-state, ublkpp's coroutine-awaitable cqe_state,
// future drivers, etc.) keeps user_data encoding consistent. A custom
// dispatch loop can use the bit to separate "managed by this consumer" from
// "delegate elsewhere" without baking any one consumer's type into the
// encoding. io_uring_scheduler is narrower: every managed non-null CQE it
// reaps must point at sisl::async::cqe_state.
constexpr uint64_t k_managed_bit = 1ULL << 63;

// Returns true if `ud` was produced by encode_managed_user_data, i.e. the
// CQE belongs to a sisl::async-aware submitter. False means the CQE should
// be delegated to whoever else shares the io_uring (ublksrv command path,
// raw consumers, etc.).
inline bool is_managed_user_data(uint64_t ud) noexcept { return 0 != (ud & k_managed_bit); }

// Encodes a pointer into a user_data value with the managed bit set.
// Type-erased on purpose for custom dispatch loops: they may recover whatever
// pointer type they own. io_uring_scheduler specifically encodes
// sisl::async::cqe_state* and decodes managed non-null values as that type.
inline uint64_t encode_managed_user_data(void* p) noexcept { return reinterpret_cast< uint64_t >(p) | k_managed_bit; }

// Strips the managed bit and returns the encoded pointer. Pre-condition:
// is_managed_user_data(ud) is true. The result MAY be nullptr -- some
// consumers use a managed-null sentinel (e.g. probe-timeout markers) that
// must be distinguished from "not managed at all"; that is what the
// is_managed_user_data check is for. If you only have a typed cqe_state
// path and don't care about sentinels, treat managed-null and not-managed
// as the same skip case.
inline void* decode_managed_user_data(uint64_t ud) noexcept { return reinterpret_cast< void* >(ud & ~k_managed_bit); }

// Per-SQE bridge between an io_uring CQE and the consumer that submitted it.
// Polymorphic via a function-pointer + opaque context: the dispatch loop
// invokes _on_complete(_on_complete_ctx, cqe->res) when the matching CQE
// arrives. Consumers wire this up however they like:
//   - stdexec sender op-state: thunk calls stdexec::set_value(receiver).
//   - C++20 coroutine awaitable bridge: thunk resumes a stored
//     coroutine_handle (caller is responsible for exception discipline
//     across resume() since completion_fn is noexcept).
//   - Synchronous wait: thunk just sets a flag.
//
// _result and _result_ready are also written by the dispatch loop for
// callers that want to peek without going through the callback path
// (e.g., debug asserts, manual wait loops).
//
// Lifetime contract:
//   - The cqe_state's address is stored in the SQE's user_data
//     (encoded via encode_managed_user_data).
//   - The owner MUST keep the cqe_state alive until the dispatch loop
//     invokes the callback. The simplest pattern is to embed the
//     cqe_state in the sender's operation_state, which lives until
//     set_value fires.
struct cqe_state {
    using completion_fn = void (*)(void* ctx, int res) noexcept;

    completion_fn _on_complete{nullptr};
    void* _on_complete_ctx{nullptr};
    int _result{0};
    bool _result_ready{false};
};

// Standard "I reaped a CQE for this state, deliver it" sequence. Used by
// io_uring_scheduler::poll_once and intended to be used by any other
// dispatch loop sharing the same io_uring (e.g. ublkpp's queue loop) so
// the writes-then-callback ordering is identical across consumers.
inline void complete_cqe_state(cqe_state& s, int res) noexcept {
    s._result = res;
    s._result_ready = true;
    if (nullptr != s._on_complete) { s._on_complete(s._on_complete_ctx, res); }
}

// C++20 coroutine awaitable adapter built on cqe_state. Derives from
// cqe_state and resumes a stored coroutine_handle<> when complete_cqe_state()
// fires.
//
// THREAD SAFETY: the consumer that co_awaits (await_suspend) and the dispatch
// loop that delivers the CQE (complete_cqe_state -> on_complete_thunk) may run
// on DIFFERENT threads. The canonical example is iomgr's off-reactor drive
// path: an application thread submits via run_on_forget(worker) and then
// co_awaits this awaitable, while the completion is reaped and delivered on a
// worker reactor thread. The two sides coordinate through a single lock-free
// atomic state word with a "whoever runs second performs the one resume"
// (a.k.a. single-slot future) protocol, restoring the cross-thread
// happens-before that folly::Future's atomic core used to provide before the
// coroutine migration. _result is published by complete_cqe_state before the
// thunk's release-exchange and consumed after the matching acquire on the
// other side, so the value handoff is race-free.
//
// Causal requirement: the completion must be delivered AFTER the consumer has
// suspended -- i.e. complete_cqe_state must not run concurrently with the
// coroutine still executing its suspend bookkeeping. Every real submitter
// satisfies this naturally: the CQE that drives complete_cqe_state can only be
// reaped after the SQE is submitted and the kernel finishes the I/O, which is
// causally long after the awaiting coroutine suspended. The atomic exchange
// publishes the awaiter state across the thread boundary; the realistic
// submit -> suspend -> kernel -> reap -> complete ordering keeps the resume of
// the suspended frame race-free.
//
// The completion callback is installed at construction (not lazily in
// await_suspend) so a completion that races ahead of await_suspend is recorded
// (flips the state to k_done) instead of being dropped because _on_complete was
// still null. Because the installed _on_complete_ctx points at *this, the
// awaitable is non-copyable/non-movable; embed it in a stable location (e.g.
// the heap iocb / coroutine frame) and keep it alive until completion fires.
//
// Exception discipline: completion_fn is noexcept, so any exception that
// propagates through _waiter.resume() calls std::terminate. Consumers that need
// to survive coroutine-body exceptions must handle them inside the coroutine
// frame.
struct cqe_awaitable : cqe_state {
    enum : uint8_t { k_init = 0, k_waiting = 1, k_done = 2 };

    std::coroutine_handle<> _waiter{};
    std::atomic< uint8_t > _state{k_init};

    cqe_awaitable() noexcept {
        _on_complete_ctx = this;
        _on_complete = &on_complete_thunk;
    }
    cqe_awaitable(const cqe_awaitable&) = delete;
    cqe_awaitable& operator=(const cqe_awaitable&) = delete;

    // Fast path: if the completion already arrived (state observed as k_done with
    // acquire ordering, which also makes _result visible) skip suspension.
    bool await_ready() const noexcept { return _state.load(std::memory_order_acquire) == k_done; }
    int await_resume() const noexcept { return _result; }

    // Returns true to stay suspended (the completer will resume us later); false to
    // resume immediately because the completion landed between await_ready and here.
    // No recursive resume() is performed.
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        _waiter = h;
        return _state.exchange(k_waiting, std::memory_order_acq_rel) != k_done;
    }

    static void on_complete_thunk(void* ctx, int /*res*/) noexcept {
        auto* const self = static_cast< cqe_awaitable* >(ctx);
        // complete_cqe_state has already written _result / _result_ready; the
        // acq_rel exchange publishes them to a consumer that observes k_done.
        if (self->_state.exchange(k_done, std::memory_order_acq_rel) == k_waiting) { self->_waiter.resume(); }
    }
};

} // namespace sisl::async
