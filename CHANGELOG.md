# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [14.0.0] - 2026-04-22

### Breaking Changes

- **C++23 required.** Minimum standard raised from C++20 to C++23.
- **cpp-libhttp** Added to replace pistache.
- **Pistache removed.** All pistache dependencies eliminated.
- **Folly removed.** All folly dependencies eliminated:
  - `folly::SharedMutex` replaced with `std::shared_mutex` in `fds/bitset.hpp`, `fds/stream_tracker.hpp`, `cache/range_hashmap.hpp`, `cache/simple_hashmap.hpp`
  - `folly::small_vector` replaced with `boost::container::small_vector`
  - `folly::Synchronized` replaced with `std::mutex` + `std::lock_guard` in metrics
  - `folly::Promise` / `folly::Future` replaced with `std::promise` / `std::future` in gRPC client
  - `folly::ThreadLocalPtr` replaced with `thread_local std::unique_ptr` in `fds/freelist_allocator.hpp`
  - `GrpcResult<T>` is now `std::expected<T, grpc::Status>`; `GrpcAsyncResult<T>` is now `std::future<GrpcResult<T>>`

### Bug Fixes

- **Lock-order inversion in `sobject::add_child()`**: per-object lock was held while acquiring the manager lock, creating a potential deadlock with concurrent `get_status()` calls.
- **Data race in `CounterValue::m_value`**: changed from plain `int64_t` to `std::atomic<int64_t>` with relaxed load/store — each slot has exactly one writer so no `LOCK` prefix is generated.
- **Dual-object lock-order inversion in `BitsetImpl::copy()`**: `copy()` + concurrent `operator==` on the same bitset pair could deadlock with writer-preference mutexes. Fixed with address-ordered locking across all dual-object methods.
- **Data race in `ThreadBuffer::get()`**: `operator[]` was observing `size()` concurrently with `emplace_back()` from thread-attach. Fixed by using `data()[tnum]` directly, bypassing `size()`.

### Dependency Changes

- **Added / updated**: boost/1.85.0, spdlog/1.17.0, nlohmann_json/3.12.0, cxxopts/3.3.1, flatbuffers/24.12.23, prometheus-cpp/1.3.0, grpc/1.69.0, gtest/1.17.0, benchmark/1.9.5
- **Removed**: folly (all versions)

[14.0.1]: https://github.com/eBay/sisl/compare/v14.0.0...dev/v14.x
[14.0.0]: https://github.com/eBay/sisl/compare/stable/v13.x...dev/v14.x
