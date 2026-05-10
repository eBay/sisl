#pragma once

#include <cstdint>

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
inline bool is_managed_user_data(uint64_t ud) noexcept {
    return 0 != (ud & k_managed_bit);
}

// Encodes a pointer into a user_data value with the managed bit set.
// Type-erased on purpose for custom dispatch loops: they may recover whatever
// pointer type they own. io_uring_scheduler specifically encodes
// sisl::async::cqe_state* and decodes managed non-null values as that type.
inline uint64_t encode_managed_user_data(void* p) noexcept {
    return reinterpret_cast< uint64_t >(p) | k_managed_bit;
}

// Strips the managed bit and returns the encoded pointer. Pre-condition:
// is_managed_user_data(ud) is true. The result MAY be nullptr -- some
// consumers use a managed-null sentinel (e.g. probe-timeout markers) that
// must be distinguished from "not managed at all"; that is what the
// is_managed_user_data check is for. If you only have a typed cqe_state
// path and don't care about sentinels, treat managed-null and not-managed
// as the same skip case.
inline void* decode_managed_user_data(uint64_t ud) noexcept {
    return reinterpret_cast< void* >(ud & ~k_managed_bit);
}

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
    void*         _on_complete_ctx{nullptr};
    int           _result{0};
    bool          _result_ready{false};
};

// Standard "I reaped a CQE for this state, deliver it" sequence. Used by
// io_uring_scheduler::poll_once and intended to be used by any other
// dispatch loop sharing the same io_uring (e.g. ublkpp's queue loop) so
// the writes-then-callback ordering is identical across consumers.
inline void complete_cqe_state(cqe_state& s, int res) noexcept {
    s._result       = res;
    s._result_ready = true;
    if (nullptr != s._on_complete) { s._on_complete(s._on_complete_ctx, res); }
}

} // namespace sisl::async
