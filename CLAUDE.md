# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

All building goes through Conan 2, which drives CMake internally.

**First-time setup** — export the vendored 3rd-party Conan recipes (run once):

```bash
./prepare_v2.sh   # exports userspace-rcu/nu2.0.14.0
```

**Common build commands:**

```bash
# Debug build + run tests (default)
conan build -s:h build_type=Debug --build missing .

# Release build, skip tests
conan build -s:h build_type=Release -c tools.build:skip_test=True --build missing .

# Address sanitizer
conan build -o sisl/*:sanitize=address -s:h build_type=Debug --build missing .

# Thread sanitizer
conan build -o sisl/*:sanitize=thread -s:h build_type=Debug --build missing .

# Coverage
conan build -o sisl/*:coverage=True -s:h build_type=Debug --build missing .
gcovr --cobertura ./coverage.xml
```

**Build output directories** are controlled by `layout()` in `conanfile.py`:

| Variant | Directory |
|---|---|
| Debug / Release | `build/Debug/` or `build/Release/` |
| Address sanitizer | `build/Sanitized-address/` |
| Thread sanitizer | `build/Sanitized-thread/` |
| Coverage | `build/Coverage/` |

**Running tests manually** after a build:

```bash
# All tests
ctest --test-dir build/Debug --output-on-failure

# Single test by CTest name (find names via grep for add_test in src/*/CMakeLists.txt)
ctest --test-dir build/Debug --output-on-failure -R MetricsFarm

# Single test binary directly, with GTest filter
./build/Debug/src/metrics/metrics_farm_test --gtest_filter="*Farm*"
```

**Build options** (passed as `-o sisl/*:<option>=<value>`):

| Option | Default | Notes |
|---|---|---|
| `sanitize` | `False` | `address`, `thread`, or `False`; Debug only |
| `malloc_impl` | `libc` | `libc`, `tcmalloc`, `jemalloc`; jemalloc disables sanitizers |
| `metrics` | `True` | Enables metrics, WISR, FDS, cache, settings subsystems |
| `grpc` | `True` | Requires `metrics=True` |
| `http` | `True` | HTTP server (cpp-httplib; cross-platform, Clang-compatible) |
| `coverage` | `False` | Debug only; incompatible with `sanitize` |

**Code formatting:**

```bash
./apply-clang-format.sh        # format in-place
./apply-clang-format.sh -v     # validate only (exits 1 if diff)
```

Style: LLVM base, 4-space indent, 120-column limit, `PointerAlignment: Left`, `SortIncludes: false`.

## Architecture

### Component Dependency DAG

The library is organized as separately-linkable Conan components. Dependencies flow:

```
sisl_options  (boost, cxxopts)
  └─ sisl_logging  (spdlog, nlohmann_json, breakpad/Linux+libstdc++; links -rdynamic)
       ├─ sisl_sobject
       ├─ sisl_file_watcher
       ├─ sisl_version  (zmarok-semver)
       └─ [metrics=True]
            sisl_metrics  (prometheus-cpp, userspace-rcu)
              └─ sisl_buffer/fds  (snappy)
                   └─ sisl_cache
            sisl_settings  (flatbuffers, userspace-rcu)
            └─ [grpc=True]
                 flip       (gRPC, protobuf — proto codegen)
                 sisl_grpc  (sisl_buffer + gRPC)
       └─ [http=True]
            sisl_http  (cpp-httplib)
```

The source lives in `src/<component>/` and the public headers in `include/sisl/`. The root CMakeLists adds `include/` globally so `#include <sisl/...>` works everywhere.

**wisr** and **utility** are header-only — no compiled library target. Their headers live in `include/sisl/utility/` even for wisr. Tests link against `sisl_metrics`.

**fds** (`src/fds/`) is named `buffer` as a Conan component and `sisl_buffer` as a CMake target. The naming mismatch is intentional.

### Settings Codegen

Components using the settings framework call `settings_gen_cpp()` from `cmake/settings_gen.cmake`. This macro runs `flatc` on a `.fbs` schema and `xxd` to produce a binary blob, generating both `<schema>_generated.h` and `<schema>_bindump.cpp` in the build directory. The generated path must be added to the target's includes.

### Proto/gRPC Codegen (flip)

`src/flip/proto/` uses `protobuf_generate()` to produce C++ and Python bindings. The result is an `OBJECT` library (`flip_proto`) consumed via `$<TARGET_OBJECTS:flip_proto>` in the `flip` library.

### Sanitizer Infrastructure

- `cmake/sanitize.cmake` — sets compiler/linker flags based on `SANITIZER_TYPE` (`"address"` or `"thread"`)
- CMakeLists.txt root checks `THREAD_SANITIZER_ON` / `ADDRESS_SANITIZER_ON` variables (set by `conanfile.py` → CMakeToolchain)
- `tsan.supp` — TSAN suppressions; tests that use it set `ENVIRONMENT "TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tsan.supp"` via `set_tests_properties`
- Tests that cannot run under TSAN (gRPC/absl false positives that can't be suppressed) are set `DISABLED TRUE` in their CMakeLists.

### CI

Four named jobs run on `ubuntu-24.04` for PRs and merges to `dev/v14.x`. All use `conan-channel: "dev"`.

| Job | Compiler | Build type | Malloc | Sanitizer |
|---|---|---|---|---|
| GccThreadSanitize | GCC | Debug | libc | thread |
| GccAddressSanitize | GCC | Debug | libc | address |
| GccCoverage | GCC | Debug | libc | none (coverage=True) |
| ClangRelease | Clang + libstdc++ | Release | tcmalloc | none |

Breakpad is excluded when `compiler.libcxx == libc++` (libc++ enforces complete-type deletion in `unique_ptr`, which breakpad violates). Clang + libstdc++ builds fine.

ChainBuild (iomanager / nuraft_mesg) is commented out pending their migration to `dev/v14.x`.
