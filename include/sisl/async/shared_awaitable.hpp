#pragma once

#include <coroutine>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace sisl::async {

// Cross-thread, multi-consumer (broadcast) awaitable carrying a value of type T.
//
// The multi-waiter generalization of value_awaitable: where value_awaitable resumes exactly ONE waiter and
// MOVES its result out (single-shot), shared_awaitable resumes ALL installed waiters and hands each a COPY of
// the result (broadcast). It is the folly::SharedPromise<T> replacement -- the pattern where several callers
// await the same in-flight operation and all observe its single completion (e.g. N threads triggering one
// checkpoint flush, or several openers of one log store). T must therefore be copyable.
//
// USAGE: one producer holds the shared_awaitable (typically inside a std::shared_ptr) and calls complete(value)
// once when the operation finishes. Each consumer co_awaits the SAME object (the object IS the awaitable, like
// value_awaitable) -- usually a coroutine that captures a std::shared_ptr to it so it stays alive across the
// suspend. A consumer that co_awaits AFTER completion takes the fast path and resumes inline.
//
// THREAD SAFETY: complete() and any number of await_suspend()/await_resume() may run on different threads. A
// single mutex guards the waiter list and the result; resumes are performed OUTSIDE the lock (the waiter list
// is swapped out under the lock, then drained) so a resumed coroutine can re-enter (await again, or complete a
// nested awaitable) without self-deadlock. The handshake has no lost-wakeup: a consumer whose await_suspend
// races a concurrent complete() either gets installed-then-resumed, or observes _done and resumes itself --
// exactly once either way.
//
// LIFETIME: the object must outlive every waiter's resume AND any late (post-completion) co_await. complete()
// resumes installed waiters inline, so they have run past await_resume by the time it returns; the canonical
// arrangement is a std::shared_ptr held by BOTH the producer and each awaiting coroutine frame, so the last
// reference (producer or consumer) keeps the result readable.
//
// EXACTLY-ONCE: complete() must be called; a second call is a no-op (the broadcast already fired). await_resume
// copies the result, so the awaitable can be observed any number of times after completion.
//
// EXCEPTION DISCIPLINE: complete() resumes waiters via handle.resume(); an exception escaping a resumed frame
// propagates out of complete() (same contract as value_awaitable). A producer running on a noexcept completion
// boundary must ensure the resumed coroutine bodies cannot throw across it.
template < typename T >
struct shared_awaitable {
    mutable std::mutex _mtx{};
    bool _done{false};
    std::optional< T > _result{};
    std::vector< std::coroutine_handle<> > _waiters{};

    shared_awaitable() = default;
    shared_awaitable(const shared_awaitable&) = delete;
    shared_awaitable& operator=(const shared_awaitable&) = delete;

    // Producer side (any thread). Publishes the value, marks done, and resumes every installed waiter. The
    // waiters are swapped out under the lock and resumed outside it so a resumed coroutine may re-enter safely.
    // A second complete() is ignored.
    void complete(T value) {
        std::vector< std::coroutine_handle<> > to_resume;
        {
            std::lock_guard lg{_mtx};
            if (_done) { return; }
            _result.emplace(std::move(value));
            _done = true;
            to_resume.swap(_waiters);
        }
        for (auto h : to_resume) {
            h.resume();
        }
    }

    // True once complete() has fired. Also the awaiter fast-path check.
    [[nodiscard]] bool is_ready() const {
        std::lock_guard lg{_mtx};
        return _done;
    }

    // ----- awaiter interface (the object is co_await-ed directly; many coroutines may await one object) -----

    [[nodiscard]] bool await_ready() const {
        std::lock_guard lg{_mtx};
        return _done;
    }

    // Install this coroutine as a waiter, unless completion already landed (return false -> resume immediately).
    bool await_suspend(std::coroutine_handle<> h) {
        std::lock_guard lg{_mtx};
        if (_done) { return false; }
        _waiters.push_back(h);
        return true;
    }

    // Broadcast: hand back a COPY so every waiter observes the result independently.
    T await_resume() const {
        std::lock_guard lg{_mtx};
        return *_result;
    }
};

} // namespace sisl::async
