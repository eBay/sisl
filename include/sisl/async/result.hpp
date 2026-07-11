#pragma once

// The asynchronous form of sisl::result: a coroutine you co_await that resolves to a sisl::result<T>. It layers
// the sisl::async::task carrier (exec::task / stdexec) over the pure-std sisl::result, so it lives beside
// task.hpp and inherits the SAME opt-in stdexec dependency -- consumers without stdexec use <sisl/result.hpp>
// directly. Because sisl::async::result<T> IS sisl::async::task<sisl::result<T>>, it composes with
// stdexec::when_all / when_any and co_awaits from any task, which is what lets higher layers gather async
// operations on one coroutine model.
//
// It sits in sisl::async (beside task) because that is the coroutine-carried world; the plain sisl::result is
// top-level vocabulary. Splitting them by namespace mirrors the header split. A consumer that wants a flat
// `async_result` name exposes its own alias: `template < class T > using async_result = sisl::async::result< T >;`.

#include <variant>

#include <sisl/async/task.hpp>
#include <sisl/result.hpp>

namespace sisl::async {

// Operational result of an asynchronous call: a coroutine that resolves to a sisl::result<T>. `sisl::result` is
// qualified deliberately -- unqualified `result` here would name THIS alias and self-refer.
template < typename T >
using result = task< sisl::result< T > >;

using status = result< std::monostate >;

} // namespace sisl::async
