#pragma once

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <system_error>
#include <utility>

#include <liburing.h>

#include <stdexec/execution.hpp>

#include <sisl/async/cqe_state.hpp>

namespace sisl::async {

// Single-threaded io_uring scheduler exposing stdexec senders. Submission,
// polling, and continuation must all happen on the SAME thread (the
// queue/worker thread that owns the io_uring). The scheduler does NOT own
// the io_uring's lifetime -- the caller is responsible for queue_init and
// queue_exit.
//
// Senders are returned directly (not wrapped in exec::task), so consumer
// coroutines can `co_await sched.schedule_at(...)` from any sender-aware
// coroutine (exec::task<T> in particular) without surprises from
// as_awaitable / continues_on interposition.
//
// Usage (single-thread driver loop):
//
//   ::io_uring ring{};
//   ::io_uring_queue_init(N, &ring, 0);
//   sisl::async::io_uring_scheduler sched(&ring);
//
//   bool done = false;
//   auto runner = [&]() -> exec::task<void> {
//       co_await sched.schedule_at(target_ns);
//       done = true;
//   };
//   exec::async_scope scope;
//   scope.spawn(runner());
//   while (!done) sched.poll_once(std::chrono::milliseconds{1});
class io_uring_scheduler {
public:
    explicit io_uring_scheduler(::io_uring* ring) noexcept : _ring(ring) {}

    io_uring_scheduler(io_uring_scheduler const&)            = delete;
    io_uring_scheduler& operator=(io_uring_scheduler const&) = delete;

    // ------- senders --------------------------------------------------------

    // sender that completes with `void` after wall_ns wall time, backed by an
    // IORING_OP_TIMEOUT SQE (IORING_TIMEOUT_ABS) on CLOCK_MONOTONIC. The
    // wakeup arrives via the same CQE path as I/O completions.
    [[nodiscard]] auto schedule_at(uint64_t wall_ns) noexcept {
        return timeout_sender{_ring, wall_ns};
    }

    // sender that completes with `int` (cqe->res) after the SQE prepared by
    // `prep` is reaped. PrepFn signature: void(io_uring_sqe*).
    template < typename PrepFn >
    [[nodiscard]] auto async_submit(PrepFn prep) noexcept {
        return submit_sender< PrepFn >{_ring, std::move(prep)};
    }

    // ------- run loop -------------------------------------------------------

    // Reap cycle. Called on the scheduler's owning thread in a loop:
    //
    //     while (!stop) sched.poll_once(std::chrono::milliseconds{1});
    //
    // Submits any SQEs queued since the last poll_once (op senders only
    // call ::io_uring_get_sqe / set user_data; submission is deferred so
    // many ops produced in a tight emission burst go through one
    // syscall, not one each), then waits up to `wait_budget` for a CQE.
    // wait_budget == 0 means non-blocking poll. Drains all currently-
    // queued CQEs in one pass, invoking each cqe_state's completion
    // thunk.
    void poll_once(std::chrono::nanoseconds wait_budget) {
        if (0 < wait_budget.count()) {
            __kernel_timespec ts{};
            ts.tv_sec  = static_cast< __kernel_time64_t >(wait_budget.count() / 1'000'000'000);
            ts.tv_nsec = static_cast< long long >(wait_budget.count() % 1'000'000'000);
            ::io_uring_cqe* unused = nullptr;
            // Submits pending SQEs and waits for one CQE in a single
            // syscall. The crucial perf piece: many op senders that
            // queued SQEs since the last poll go out together.
            (void)::io_uring_submit_and_wait_timeout(_ring, &unused, 1, &ts, nullptr);
        } else {
            // No wait, but still flush any pending SQEs.
            (void)::io_uring_submit(_ring);
        }

        ::io_uring_cqe* cqe = nullptr;
        unsigned head       = 0;
        unsigned reaped     = 0;
        io_uring_for_each_cqe(_ring, head, cqe) {
            uint64_t const ud = ::io_uring_cqe_get_data64(cqe);
            // Two-step decode: bit check first, then strip. This scheduler
            // assumes it owns the ring, or at least that every managed
            // non-null CQE it reaps points at sisl::async::cqe_state.
            // Managed-null and non-managed CQEs are skipped here, but still
            // consumed by cq_advance below; mixed-owner rings need a custom
            // dispatch loop that delegates before advancing.
            if (is_managed_user_data(ud)) {
                auto* const state = static_cast< cqe_state* >(decode_managed_user_data(ud));
                if (nullptr != state) { complete_cqe_state(*state, cqe->res); }
            }
            ++reaped;
        }
        if (0 < reaped) { ::io_uring_cq_advance(_ring, reaped); }
    }

    ::io_uring* ring() noexcept { return _ring; }

private:
    // ------- timeout_sender -------------------------------------------------

    // Acquires an SQE from the ring, flushing any pending SQEs first if
    // the ring is full. Returns nullptr only if the kernel cannot allocate
    // one even after submission -- caller should treat as transient
    // back-pressure. Same idiom as ublkpp's lib/cqe_state.hpp::next_sqe.
    static ::io_uring_sqe* acquire_sqe(::io_uring* ring) noexcept {
        ::io_uring_sqe* sqe = ::io_uring_get_sqe(ring);
        if (nullptr != sqe) { return sqe; }
        // Ring full: flush queued SQEs to make room. After a successful
        // submit the SQ is empty and get_sqe returns a fresh slot.
        if (0 > ::io_uring_submit(ring)) { return nullptr; }
        return ::io_uring_get_sqe(ring);
    }

    template < typename Receiver >
    struct timeout_op {
        ::io_uring*       _ring;
        uint64_t          _wall_ns;
        Receiver          _receiver;
        cqe_state         _state{};
        __kernel_timespec _ts{};

        void start() noexcept {
            _ts.tv_sec  = static_cast< __kernel_time64_t >(_wall_ns / 1'000'000'000ULL);
            _ts.tv_nsec = static_cast< long long >(_wall_ns % 1'000'000'000ULL);

            _state._on_complete_ctx = this;
            _state._on_complete     = +[](void* ctx, int /*res*/) noexcept {
                auto* const self = static_cast< timeout_op* >(ctx);
                stdexec::set_value(std::move(self->_receiver));
            };

            ::io_uring_sqe* const sqe = acquire_sqe(_ring);
            if (nullptr == sqe) {
                // Should be unreachable after the submit-on-full retry,
                // but honesty matters: do NOT silently set_value() as if
                // the timer fired. Surface through the error channel so
                // a Poisson loop awaiting schedule_at sees a real error
                // instead of waking early. The exec::task awaiting this
                // sender rethrows the captured exception_ptr.
                try {
                    throw std::system_error{ENOMEM, std::system_category(),
                                             "io_uring_scheduler: SQ exhausted on schedule_at"};
                } catch (...) { stdexec::set_error(std::move(_receiver), std::current_exception()); }
                return;
            }
            ::io_uring_prep_timeout(sqe, &_ts, 0, IORING_TIMEOUT_ABS);
            ::io_uring_sqe_set_data64(sqe, encode_managed_user_data(&_state));
            // Submission is DEFERRED to poll_once for batching. This is
            // the difference between "syscall per op" and "syscall per
            // poll cycle"; at QD=32 it's a ~30x cut in syscalls.
        }
    };

    struct timeout_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures =
            stdexec::completion_signatures< stdexec::set_value_t(), stdexec::set_error_t(std::exception_ptr) >;

        ::io_uring* _ring;
        uint64_t    _wall_ns;

        template < typename Receiver >
        timeout_op< std::decay_t< Receiver > > connect(Receiver&& r) const noexcept {
            return {_ring, _wall_ns, std::forward< Receiver >(r), {}, {}};
        }
    };

    // ------- submit_sender --------------------------------------------------

    template < typename PrepFn, typename Receiver >
    struct submit_op {
        using stop_token_t = stdexec::stop_token_of_t< stdexec::env_of_t< Receiver > >;

        // When the receiver's stop token is signalled (on the scheduler's own thread -- consistent
        // with the single-thread contract), submit an async-cancel for the in-flight op. The cancel
        // MUST be keyed on the ENCODED user_data (the same value set on the original SQE); keying on
        // the raw &_state returns -ENOENT. The cancel's own CQE is left unmanaged (user_data 0) so
        // poll_once skips it; the original op then completes with -ECANCELED and we deliver set_stopped.
        struct on_stop {
            submit_op* _self;
            void operator()() noexcept {
                ::io_uring_sqe* const sqe = acquire_sqe(_self->_ring);
                if (nullptr != sqe) {
                    ::io_uring_prep_cancel64(sqe, encode_managed_user_data(&_self->_state), 0);
                    ::io_uring_sqe_set_data64(sqe, 0);
                }
            }
        };
        using stop_callback_t = typename stop_token_t::template callback_type< on_stop >;

        ::io_uring*                      _ring;
        PrepFn                           _prep;
        Receiver                         _receiver;
        cqe_state                        _state{};
        std::optional< stop_callback_t > _on_stop{};

        void start() noexcept {
            _state._on_complete_ctx = this;
            _state._on_complete     = +[](void* ctx, int res) noexcept {
                auto* const self = static_cast< submit_op* >(ctx);
                self->_on_stop.reset(); // de-register before completing (dtor joins any in-flight callback)
                if (-ECANCELED == res) {
                    stdexec::set_stopped(std::move(self->_receiver));
                } else {
                    stdexec::set_value(std::move(self->_receiver), res);
                }
            };

            ::io_uring_sqe* const sqe = acquire_sqe(_ring);
            if (nullptr == sqe) {
                // For async_submit the int value channel can carry a
                // negative errno (mirroring cqe->res semantics), but a
                // ring-full condition is a scheduler-level failure
                // distinct from a kernel I/O error -- surface through
                // the error channel so callers can distinguish.
                try {
                    throw std::system_error{ENOMEM, std::system_category(),
                                             "io_uring_scheduler: SQ exhausted on async_submit"};
                } catch (...) { stdexec::set_error(std::move(_receiver), std::current_exception()); }
                return;
            }
            _prep(sqe);
            ::io_uring_sqe_set_data64(sqe, encode_managed_user_data(&_state));

            // Arm cancellation. Skipped entirely (no member overhead beyond the empty optional) for
            // unstoppable tokens, so non-cancelling callers pay nothing.
            if constexpr (!stdexec::unstoppable_token< stop_token_t >) {
                _on_stop.emplace(stdexec::get_stop_token(stdexec::get_env(_receiver)), on_stop{this});
            }
        }
    };

    template < typename PrepFn >
    struct submit_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures =
            stdexec::completion_signatures< stdexec::set_value_t(int), stdexec::set_error_t(std::exception_ptr),
                                            stdexec::set_stopped_t() >;

        ::io_uring* _ring;
        PrepFn      _prep;

        template < typename Receiver >
        submit_op< PrepFn, std::decay_t< Receiver > > connect(Receiver&& r) && noexcept {
            return {_ring, std::move(_prep), std::forward< Receiver >(r)};
        }
        template < typename Receiver >
        submit_op< PrepFn, std::decay_t< Receiver > > connect(Receiver&& r) const& noexcept {
            return {_ring, _prep, std::forward< Receiver >(r)};
        }
    };

    ::io_uring* _ring;
};

} // namespace sisl::async
