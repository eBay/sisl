# SymbiosisLib (sisl)

[![Conan Build (stable/v13.x)](https://github.com/eBay/sisl/actions/workflows/merge_build.yml/badge.svg?branch=stable%2Fv13.x)](https://github.com/eBay/sisl/actions/workflows/merge_build.yml)
[![Conan Build (dev/v14.x)](https://github.com/eBay/sisl/actions/workflows/merge_build.yml/badge.svg?branch=dev%2Fv14.x)](https://github.com/eBay/sisl/actions/workflows/merge_build.yml)
[![CodeCov](https://codecov.io/gh/szmyd/sisl/branch/master/graph/badge.svg)](https://codecov.io/gh/szmyd/sisl)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

> *Pronounced "sizzle" — because your code should too.*

A C++23 library of high-performance data structures, utilities, and infrastructure components for systems that can't afford to be slow. sisl sits on top of Boost and the STL — filling gaps, wrapping complexity, and outperforming general-purpose approaches where it matters most.

## Table of Contents

- [Quick Start](#quick-start)
- [Components](#components)
  - [Logging](#logging)
  - [Metrics](#metrics)
  - [WISR](#wisr--wait-free-inserts-snoozy-reads)
  - [FDS — Fast Data Structures](#fds--fast-data-structures)
  - [Cache](#cache)
  - [Settings](#settings)
  - [Flip — Fault Injection](#flip--fault-injection)
  - [gRPC Utilities](#grpc-utilities)
  - [Auth Manager](#auth-manager)
  - [Utility](#utility)
  - [Sobject](#sobject)
  - [File Watcher](#file-watcher)
  - [HTTP Server](#http-server)
- [Platform Support](#platform-support)
- [Contributing](#contributing)
- [License](#license)

---

## Quick Start

### Prerequisites

- Conan 2.0+
- CMake 3.22+
- C++23-capable compiler: GCC 14+ or Clang 17+

### Build

```bash
git clone git@github.com:eBay/sisl
cd sisl
./prepare_v2.sh          # export local recipes to the conan cache
conan build -s:h build_type=Debug --build missing .
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `metrics` | `True` | Metrics, WISR, FDS, Cache, and Settings components |
| `grpc` | `True` | gRPC transport and Flip fault injection (requires `metrics`) |
| `http` | `True` | HTTP server component built on Pistache (Linux only) |
| `malloc_impl` | `libc` | Memory allocator: `libc`, `tcmalloc`, or `jemalloc` |
| `sanitize` | `False` | Enable sanitizer: `address` (AddressSanitizer + UBSan) or `thread` (ThreadSanitizer) |
| `coverage` | `False` | Enable gcov code coverage |

```bash
# Release build with tcmalloc, skip tests
conan build -s:h build_type=Release \
    -o sisl/*:malloc_impl=tcmalloc \
    -c tools.build:skip_test=True .

# Debug + AddressSanitizer
conan build -s:h build_type=Debug -o sisl/*:sanitize=address .

# Coverage report
conan build -s:h build_type=Debug -o sisl/*:coverage=True .
```

---

## Components

### Logging

Structured logging built on [spdlog](https://github.com/gabime/spdlog) with per-module log levels, signal-based stacktrace capture, and assertion macros that log and abort.

```cpp
SISL_LOGGING_DECL(my_module)          // declare module in header
SISL_LOGGING_DEF(my_module)           // define module in one .cpp

LOGINFO("Starting component {}", name);
LOGDEBUGMOD(my_module, "detail: val={}", val);
LOGWARN("Slow path taken: {}", reason);

// Assert macros log + abort on failure
RELEASE_ASSERT(ptr != nullptr, "Expected non-null pointer");
RELEASE_ASSERT_EQ(count, expected, "Count mismatch after flush");
DEBUG_ASSERT_LT(index, size, "Index out of bounds");
```

Log levels are controlled per-module at runtime via `SetModuleLogLevel`. The `DLOG*` variants compile out in Release builds.

### Metrics

High-performance counters, gauges, and histograms with [Prometheus](https://prometheus.io/) export and JSON reporting. The write path costs **under 5 ns per update** regardless of thread count.

Two collection strategies ship out of the box:

- **ThreadBuffer + Signal** (`group_impl_type_t::thread_buf_signal`): per-thread lock-free accumulation; a signal flushes all threads before a scrape.
- **WISR RCU** (`group_impl_type_t::rcu`): wait-free inserts via the WISR framework (see below).

```cpp
class MyMetrics : public sisl::MetricsGroup {
public:
    explicit MyMetrics(std::string const& inst) :
            sisl::MetricsGroup("MyComponent", inst) {
        REGISTER_COUNTER(requests, "Total requests");
        REGISTER_GAUGE(queue_depth, "Current queue depth");
        REGISTER_HISTOGRAM(latency_us, "Latency in microseconds");
        register_me_to_farm();
    }
    DECL_COUNTER(requests);
    DECL_GAUGE(queue_depth);
    DECL_HISTOGRAM(latency_us);
};

MyMetrics m{"instance1"};
COUNTER_INCREMENT(m, requests);
GAUGE_UPDATE(m, queue_depth, q.size());
HISTOGRAM_OBSERVE(m, latency_us, elapsed_us);
```

See [src/metrics/README.md](src/metrics/README.md) for internals and histogram bucket configuration.

### WISR — Wait-free Inserts, Snoozy Reads

A concurrency framework for the pattern of *frequent fast writes, infrequent slow reads*. Per-thread buffers allow writers to proceed without any synchronization. A reader acquires a single mutex, rotates all thread-local buffers, and merges them — amortizing the read cost across all writers.

Benchmarks: **10–12× better write throughput vs. `std::mutex`** on 8 threads.

Ships with ready-to-use containers:

```cpp
sisl::wisr_vector< Request > pending;

// Writers (any thread, wait-free):
pending.push_back(req);

// Reader (periodic flush):
auto snapshot = pending.now();   // drains all threads into a local vector
for (auto& r : *snapshot) { process(r); }
```

`wisr_list` and `wisr_deque` follow the same pattern. See [src/wisr/README.md](src/wisr/README.md).

### FDS — Fast Data Structures

#### Bitset

Concurrent, dynamically-resizable bitset with bulk operations. Unlike `std::bitset` (compile-time size) and `boost::dynamic_bitset` (no concurrent access), sisl's `Bitset` supports serialization, shrink/expand, and concurrent `set`/`reset`/`test` with a `std::shared_mutex` protecting the structure.

```cpp
sisl::Bitset bs(1024);
bs.set_bit(42);
auto next = bs.get_next_set_bit(0);   // → 42
bs.serialize(buf);
```

#### StreamTracker

Tracks a stream of in-flight integer-keyed operations and computes the contiguous completed prefix without exclusive locking. Essential for ordered-completion protocols (e.g., log sequencers, replication pipelines).

```cpp
sisl::StreamTracker< MyState > tracker;
tracker.create(seq, state);       // register in-flight op
tracker.complete(seq);            // mark done
auto upto = tracker.completed_upto();  // highest contiguous completion
```

#### ThreadBuffer

Per-thread object storage that survives thread exit. The backbone of the metrics subsystem and WISR framework — allocated objects are not lost when a thread terminates.

#### ObjectAllocator (FreelistAllocator)

Thread-local free-list allocator for fixed-size objects. Pre-allocates a slab per thread and recycles in O(1) without size search — measurably faster than tcmalloc/jemalloc for single-size hot-path allocations.

```cpp
auto* obj = sisl::ObjectAllocator< MyObj >::make_object(args...);
sisl::ObjectAllocator< MyObj >::free_object(obj);
```

#### ConcurrentInsertVector

Lock-free, append-only vector for concurrent producers. Readers take a snapshot.

#### io_blob / io_blob_list_t

Byte buffer type with optional alignment and owned/borrowed semantics. The standard currency for I/O in the eBay storage stack.

```cpp
sisl::io_blob buf(4096, 512);           // 4 KiB, 512-byte aligned
std::memcpy(buf.bytes(), src, 4096);

sisl::io_blob_list_t scatter;           // small_vector<io_blob, 4>
scatter.emplace_back(buf);
```

### Cache

An LRU evictor and range-aware concurrent hashmap built on top of FDS primitives. Supports point and range lookups, pluggable eviction callbacks, and configurable bucket counts.

```cpp
sisl::RangeHashMap< uint64_t, Page > cache(num_buckets);
cache.insert(key, page);
auto result = cache.get(key);
cache.remove_range(start_key, end_key);
```

### Settings

Flatbuffers-backed runtime configuration with hot-swap support. Define a schema, generate C++ accessors at build time, and get thread-safe near-zero-cost config reads at runtime — without recompiling to change a tuning knob.

```cpp
// Generated from schema.fbs via cmake/settings_gen.cmake:
auto& cfg = Settings::instance().get();
auto timeout = cfg.network().connect_timeout_ms();

// Hot-reload from file or string:
Settings::instance().reload("/etc/myapp/config.json");
```

Supports hot-swappable (live reload) and cold (restart-required) settings in the same schema. See [src/settings/README.md](src/settings/README.md).

### Flip — Fault Injection

A gRPC-backed fault injection framework for testing failure scenarios without recompilation. Instrument code with named flip points; trigger faults externally via gRPC, Python client, or a local `FlipClient` in unit tests.

```cpp
// In production code:
FLIP_POINT("write_io_error");

// In tests:
FlipClient fc(FlipRPCClient::instance());
fc.inject_retval< int >("write_io_error", -EIO);
```

Supports boolean flips, return-value injection, async delay injection, callback flips, parameterized conditions, and frequency control (N times, every Nth, X% probability). See [src/flip/README.md](src/flip/README.md).

### gRPC Utilities

Async and sync client/server helpers on top of sisl's buffer and metrics infrastructure.

```cpp
// Async client — future-based
auto stub = client->make_stub< EchoService >("worker-1");
AsyncResult< EchoReply > fut =
    stub->call_unary< EchoRequest, EchoReply >(req,
        &EchoService::StubInterface::AsyncEcho, /*deadline_s=*/5);
auto result = fut.get();   // Result<EchoReply> = std::expected<EchoReply, grpc::Status>
if (!result) { LOGERROR("RPC failed: {}", result.error().error_message()); }

// Async client — callback-based
stub->call_unary< EchoRequest, EchoReply >(req,
    &EchoService::StubInterface::AsyncEcho,
    [](EchoReply& reply, grpc::Status& s) { /* handle */ }, 5);
```

`GrpcAsyncClientWorker` manages the completion queue and worker threads. `GrpcServer` handles registration of async services and RPC handlers.

### Auth Manager

Token-based authentication client for gRPC channels. Fetches and caches bearer tokens, automatically refreshing before expiry.

```cpp
auto token_client = std::make_shared< sisl::GrpcTokenClient >(config);
auto grpc_client = std::make_unique< GrpcAsyncClient >(
    addr, token_client, domain, ssl_cert);
```

### Utility

#### atomic_counter

Compound atomic operations with correct acquire/release fencing — patterns that are verbose and error-prone to write with raw `std::atomic`.

```cpp
sisl::atomic_counter< int32_t > ref{0};

ref.increment();
if (ref.decrement_testz()) { /* last reference — safe to delete */ }

// Increment and check if we hit the threshold exactly:
if (ref.increment_test_eq(max_outstanding)) { /* trigger backpressure */ }
```

`decrement_testz`, `increment_test_ge`, `decrement_test_le`, and their `_with_count` variants are all provided with proper fencing.

#### enum

Bidirectional name ↔ value mapping for enums, including support for bit-shifted values. C++23's `std::to_underlying` converts enum → integer; this adds string lookup in both directions.

```cpp
ENUM(MyState, uint8_t, INIT, RUNNING, STOPPED)

MyState s = MyState::RUNNING;
std::string name = enum_name(s);           // → "RUNNING"
MyState parsed = enum_value< MyState >("STOPPED");
```

#### thread_factory / name_thread

Thread creation helpers that set the OS-level thread name (visible in `htop`, `gdb`, `perf`). Standard `std::thread` and `std::jthread` have no portable equivalent.

```cpp
auto t = sisl::named_thread("io-worker", [&]{ run_loop(); });

// Or name an existing thread/jthread:
sisl::name_thread(t, "compaction");
```

#### obj_life_counter

CRTP mixin that tracks the count of live instances and total allocations of a class, exposed as sisl metrics. Useful for detecting object leaks in production without a debugger.

```cpp
class AsyncRpc : public sisl::ObjLifeCounter< AsyncRpc > { ... };

// In a metrics scrape:
// sisl_obj_life_counter{class="AsyncRpc",type="alive"} 42
// sisl_obj_life_counter{class="AsyncRpc",type="total"} 10000
```

#### non_null_ptr

A `std::unique_ptr` wrapper that guarantees the pointer is never null by default-constructing `T` when given a null or default-initialized pointer.

#### status_factory

RCU-protected status snapshot: readers get lock-free access, writers do copy-on-write under a mutex. Safe for high-read, occasional-write status objects.

```cpp
sisl::StatusFactory< ComponentStatus > status{default_args...};

status.readable([](const ComponentStatus* s) {
    // lock-free read
});
status.updateable([](ComponentStatus* s) {
    s->bytes_written += delta;   // copy-on-write under mutex
});
```

#### urcu_helper

RAII wrappers around [userspace-rcu](https://liburcu.org/) primitives (`rcu_read_lock`/`unlock`, `synchronize_rcu`, `rcu_dereference`).

### Sobject

Introspectable managed objects with JSON status reporting. Register a callback that produces a JSON blob describing your component; the `sobject_manager` aggregates them into a tree queryable by type, name, or path.

```cpp
auto obj = mgr.create_object("volume", vol_name, [&](const status_request& req) {
    status_response r;
    r.json["size_gb"] = size / Gi;
    r.json["state"] = to_string(state);
    return r;
});
obj->add_child(child_obj);

// Query all volumes:
auto resp = mgr.get_status({.obj_type = "volume", .do_recurse = true});
```

### File Watcher

Inotify-based file change notifications with callback registration.

```cpp
sisl::FileWatcher watcher;
watcher.register_listener("/etc/myapp/config.json", "cfg-reload",
    [](const std::string& path, bool deleted) {
        if (!deleted) Settings::instance().reload(path);
    });
```

### HTTP Server

A Pistache-based HTTP server with middleware auth, SSL support, and per-route access control (Linux only). Routes are classified as `localhost` (local callers only), `safe` (no auth), or `regular` (subject to token verification).

```cpp
sisl::HttpServer server{5000, /*threads=*/4, /*max_request_size=*/4000000, token_verifier};

server.setup_routes({
    {Pistache::Http::Method::Get,  "/status", handle_status, sisl::url_type::safe},
    {Pistache::Http::Method::Post, "/config", handle_config, sisl::url_type::regular},
});

// When compiled with metrics=True, wire up /metrics scrape automatically:
server.register_metrics_endpoint();

server.start();
// ...
server.stop();
```

SSL can be enabled at construction or hot-swapped at runtime:

```cpp
// SSL from the start:
sisl::HttpServer secure{ssl_cert, ssl_key, 5443, 4, 4000000, token_verifier};

// Hot-swap certs without dropping the port:
server.restart(new_cert, new_key);
```

---

## Platform Support

| Platform | Status |
|----------|--------|
| Linux x86_64 (GCC) | Fully supported |
| Linux x86_64 (Clang) | Supported — crash dumps (breakpad) and HTTP server not available |
| Linux ARM64 | Supported |
| macOS (AppleClang) | Supported — crash dumps (breakpad) and HTTP server not available |
| Windows | Not supported |

---

## Contributing

Issues, bug reports, and pull requests are welcome. Please:

1. Follow the code style — run `clang-format -style=file -i` on every modified file
2. Add tests for new functionality (`src/<component>/tests/`)
3. Ensure tests pass with AddressSanitizer (`-o sisl/*:sanitize=address`) and ThreadSanitizer (`-o sisl/*:sanitize=thread`)
4. Submit pull requests against `dev/v14.x` (active development branch)

---

## License

Copyright 2021 eBay Inc.

Primary Author: Harihara Kadayam

Developers: Harihara Kadayam, Rishabh Mittal, Bryan Zimmerman, Brian Szmyd

Licensed under the [Apache License, Version 2.0](https://www.apache.org/licenses/LICENSE-2.0). See [LICENSE](LICENSE) for full terms.
