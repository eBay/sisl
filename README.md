# SymbiosisLib (sisl)
[![Conan Build](https://github.com/eBay/sisl/actions/workflows/merge_build.yml/badge.svg?branch=master)](https://github.com/eBay/sisl/actions/workflows/merge_build.yml)
[![CodeCov](https://codecov.io/gh/eBay/sisl/branch/master/graph/badge.svg)](https://codecov.io/gh/eBay/Sisl)

> *Pronounced "sizzle" — because your code should too.*

A C++23 library of high-performance data structures, utilities, and infrastructure components built for systems that
can't afford to be slow. sisl lives on top of Boost and the STL — filling gaps, wrapping complexity, and
outperforming the standard approaches where it matters most.

The library spans three categories:
- **Higher-performance** alternatives for specific hot-path use cases
- **Simplifying wrappers** around existing libraries
- **Gap-fillers** for things that just aren't in the standard toolkit yet

---

## QuickStart

```bash
git clone git@github.com:eBay/sisl
cd sisl
./prepare_v2.sh          # export local recipes to the conan cache
conan build -s:h build_type=Debug --build missing .
```

> **Note:** Requires [Conan 2](https://conan.io/) and a C++23-capable compiler (GCC 14+ / Clang 17+).

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `metrics` | `True` | Metrics, WISR, FDS, Cache, and Settings components |
| `grpc` | `True` | gRPC transport and Flip fault injection |
| `malloc_impl` | `libc` | Memory allocator: `libc`, `tcmalloc`, or `jemalloc` |
| `sanitize` | `False` | Enable AddressSanitizer + UBSan (Debug only) |
| `coverage` | `False` | Enable gcov code coverage (Debug only) |

```bash
# Example: release build with jemalloc, no tests
conan build -s:h build_type=Release -o sisl/*:malloc_impl=jemalloc \
    -c tools.build:skip_test=True .
```

---

## What's Inside

### Metrics
High-performance metrics collection (counters, histograms, gauges) with Prometheus export and JSON reporting.
Designed to scale across hundreds of threads with negligible overhead on the write path — **<5ns per metric update**.
Gathering/scraping cost is amortized since it's rare.

Implements two collection strategies:
- **ThreadBuffer + Signal**: Per-thread lock-free collection with signal-based cache flush before scraping
- **WISR RCU**: Wait-free inserts via the WISR framework (see below)

See [src/metrics/README.md](src/metrics/README.md) for internals.

### WISR — Wait-free Inserts, Snoozy Rest
A concurrency framework for the common pattern of *frequent fast writes, infrequent slow reads*. Uses RCU to
provide wait-free inserts at near-zero overhead while reads gather and merge per-thread state.

Benchmarks show **10–12× better write throughput** vs. mutex on 8 threads, with 1.5× better reads than
`shared_mutex`. Ships with `wisr_vector`, `wisr_list`, and `wisr_deque`.

See [src/wisr/README.md](src/wisr/README.md) for the full story.

### FDS — Fast Data Structures
A collection of data structures for high-performance or specialized use cases:

- **Bitset** — Concurrent, resizable, serializable bitset with bulk operations that
  `std::bitset` and `boost::dynamic_bitset` don't provide
- **StreamTracker** — Tracks sequential integer-keyed completions and sweeps contiguous ranges;
  essential for stream processing without exclusive locks
- **ThreadBuffer** — Per-thread object storage that survives thread exit, the backbone of WISR
  and the metrics subsystem
- **ObjectAllocator** — Pool allocator for fixed-size objects with metrics integration
- **VectorPool** — Thread-local vector recycling to avoid repeated heap allocation
- **ConcurrentInsertVector** — Lock-free append-only vector for concurrent producers

### Cache
An LRU evictor and range-aware cache built on top of FDS primitives. Supports range-based lookups,
range hashmaps, and pluggable eviction callbacks.

### Settings Framework
Flatbuffers-backed runtime configuration with hot-swap support. Define your schema in a `.fbs` file,
generate the C++ accessors at build time, and get thread-safe, memory-safe, near-zero-cost config access
at runtime — without recompiling to change a tuning knob.

Supports both hot-swappable (live reload) and cold (restart-required) settings within the same schema.

See [src/settings/README.md](src/settings/README.md) for schema definition and CMake integration.

### Flip — Fault Injection Framework
A gRPC-backed fault injection framework for testing failure scenarios without recompilation. Instrument
your code with named flip points; trigger faults externally via gRPC, Python client, or local `FlipClient`
in unit tests.

Supports:
- **Boolean flips** — take an alternate code path
- **Return value injection** — return a specific error from outside the binary
- **Async delay injection** — simulate timeouts in async code
- **Callback flips** — run arbitrary logic at the injection point (local client only)
- **Parameterized conditions** — filter by runtime values with `==`, `>`, `<`, `!=`, etc.
- **Frequency control** — trigger N times, every Nth call, or at X% probability

See [src/flip/README.md](src/flip/README.md) for the full API and examples.

### Logging
Structured logging built on spdlog with stacktrace capture (via Breakpad/libunwind), backtrace support,
and `rdynamic` symbol resolution. Integrates with the Options framework for log-level control at startup.

### gRPC Utilities
Helpers for building gRPC services on top of sisl's buffer and metrics infrastructure.

### Supporting Components
- **Options** — Command-line option parsing (cxxopts wrapper) integrated with Settings
- **Sobject** — Introspectable managed objects with JSON serialization
- **FileWatcher** — Inotify-based file change notifications
- **Version** — Semver-aware build version embedding

---

## Platform Support

| Platform | Status |
|----------|--------|
| Linux x86_64 | Fully supported |
| Linux ARM | Supported (requires unreleased libunwind) |
| macOS | Partial — Metrics/WISR/FDS not supported |
| Windows | Not supported |

---

## Contributing

Issues, bug reports, and pull requests are welcome. If you find something broken, missing, or improvable —
open an issue or send a PR.

---

## License

Copyright 2021 eBay Inc.

Primary Author: Harihara Kadayam

Developers: Harihara Kadayam, Rishabh Mittal, Bryan Zimmerman, Brian Szmyd

Licensed under the [Apache License, Version 2.0](https://www.apache.org/licenses/LICENSE-2.0). See LICENSE for full terms.
