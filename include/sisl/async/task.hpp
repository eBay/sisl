#pragma once

// The stack-wide coroutine/sender currency. sisl::async::task<T> is exec::task<T>
// (stdexec). It is BOTH an awaitable (co_await from any coroutine) AND a sender
// (composes with stdexec::when_all / when_any), which is what lets higher layers --
// nuraft_mesg's AsyncResult, homestore's replication tasks -- gather async
// operations (the collectAll/when_all fan-outs) on a single coroutine model.
//
// This header requires stdexec on the include path. sisl's own library targets do
// NOT depend on stdexec; this is an opt-in header for consumers that have it (iomgr,
// nuraft_mesg, homestore -- all co-built against the same stdexec). Lower-level sisl
// bridges that must stay stdexec-free (e.g. the gRPC coroutine reply in
// sisl/grpc/rpc_client.hpp) use sisl::async::value_awaitable instead and let a
// consuming exec::task wrap them.

#include <exec/task.hpp>

namespace sisl::async {

template < typename T >
using task = exec::task< T >;

} // namespace sisl::async
