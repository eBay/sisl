#pragma once

// The stack-wide operational-result vocabulary. Every operation reports success-or-failure the SAME way: it
// returns a value or a std::error_condition. sisl::result<T> is that value-or-condition. The specific reason
// rides in the std::error_condition, whose category-registered domain enum -- homestore's ReplServiceError,
// iomgr's codes, CRAFT's craft_error, ... -- the caller branches on:
//
//     auto r = repl_dev->create(...);
//     if (!r) {
//         if (r.error() == ReplServiceError::NOT_LEADER) { /* redirect */ }
//         else { LOGERROR("create failed: {}", r.error().message()); }
//     }
//
// This header stays pure-std: it names NO domain, so any layer's error enum flows through result<T> without
// sisl knowing it. That type erasure is exactly what lets a leaf library (a wire codec, a client) speak the
// same result vocabulary as homestore without depending on it. The asynchronous form (sisl::async_result<T>,
// a co_await-able task) layers stdexec on top in <sisl/async/result.hpp>; keep this one dependency-free so
// non-async and stdexec-free consumers can still use it.
//
// Reserve exceptions for precondition violations that signal a caller bug, never for operational failures
// that are part of the contract.

#include <expected>
#include <system_error>
#include <variant>

namespace sisl {

// Operational result of a synchronous call: a value, or a std::error_condition describing the failure.
template < typename T >
using result = std::expected< T, std::error_condition >;

// A result that carries no value on success (just success/failure).
using status = result< std::monostate >;

inline status ok() noexcept { return status{std::monostate{}}; }

} // namespace sisl
