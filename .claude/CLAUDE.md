# sisl — System Infrastructure Support Library

C++ library providing fast data structures, logging, metrics, gRPC helpers, and utilities for high-performance storage/distributed systems components.

## Claude Workflow

For EVERY non-trivial task (code changes, bug fixes, features):

### 1. **ALWAYS Start with Planning**
- Use `EnterPlanMode` for any code implementation task
- Explore codebase to understand context
- **Ask questions during planning** — if anything is unclear, ask before committing
- Design approach before writing code
- Present plan for approval, including cost/impact analysis for any change that touches public headers or API types

**When to plan:** Multi-file changes, bug investigations, new features, refactoring, performance work
**Skip planning:** One-line fixes, docs-only, pure research

**Challenging proposals:**
- Think critically about user-proposed solutions during planning
- If a proposal seems suboptimal: explain concerns, present trade-offs, suggest alternatives, let user decide
- Constructive pushback is valuable — don't blindly accept

### 2. **Execute the Plan**
- Implement approved approach
- Follow development workflow below
- Stay focused on plan scope — no drive-by cleanup

### 3. **ALWAYS Finish with Review & Analysis**
After ANY task, perform self-review:
- Check for race conditions, memory leaks, edge cases
- Verify tests cover changes
- Confirm formatting applied
- Look for security vulnerabilities, performance issues
- Validate error handling
- Report findings, concerns, trade-offs

**This review is MANDATORY** — never skip.

## Build Environment

```bash
# Install deps and build (Debug, all options on)
conan install . --build missing
conan build .

# With explicit options
conan install . --build missing -o metrics=True -o grpc=True
conan install . --build missing -o sanitize=True
conan install . --build missing -o coverage=True

# Run tests directly after build
cd build/Debug && ctest --output-on-failure
```

**Format on every edit:**
```bash
clang-format -style=file -i -fallback-style=none <file>
```
Apply to every `.cpp`, `.hpp`, `.h`, `.ipp` file you modify, immediately after editing.

## Code Conventions

**Style:** 4-space indent, 120-char lines, `#pragma once`, left pointer alignment (`Type* ptr`), C++23, LLVM base style (see `.clang-format`)

**Naming:**
- Classes/structs: `PascalCase` (`MetricsGroup`, `GrpcAsyncClient`)
- Functions: `snake_case` (`register_counter`, `create_worker`)
- Instance members: `m_snake_case` (`m_promise`, `m_lock`, `m_impl`)
- Static members: `s_snake_case` (`s_workers`, `s_workers_mtx`)
- Type aliases: `snake_case_t` (`io_blob_list_t`, `sg_iovs_t`)
- Macros/enums: `SCREAMING_SNAKE_CASE`
- Namespace: `sisl`

**Error handling:**
- Use `std::expected<T, E>` (C++23) for fallible operations
- `Result<T>` = `std::expected<T, grpc::Status>` in the gRPC layer
- Assert macros: `RELEASE_ASSERT`, `DEBUG_ASSERT`, `LOGMSG_ASSERT`

**Logging macros** (from `sisl/logging/logging.h`):
- `LOGINFO`, `LOGDEBUG`, `LOGWARN`, `LOGERROR`, `LOGCRITICAL`
- `LOGDEBUGMOD(module, ...)`, `LOGWARNMOD(module, ...)` — module-scoped variants
- Declare modules with `SISL_LOGGING_DECL(module_name)`

**No unnecessary comments** — only add a comment when the WHY is non-obvious (hidden constraint, subtle invariant, workaround). Don't describe what the code does.

## Development Workflow

**Every code change:**
1. Write code
2. Write tests (unless change is docs, build config, or otherwise untestable)
3. Run `clang-format -style=file -i -fallback-style=none` on each edited file
4. Build: `~/.venv/conan/bin/conan build .` (auto-runs ctest)

## Testing Guidelines

**Organization:**
- Location: `src/<component>/tests/`
- Naming: `test_*.cpp` or `*_test.cpp`

**Framework:** Google Test (GTest)
- `EXPECT_*` for non-fatal assertions, `ASSERT_*` for fatal
- Integration tests preferred over mocks where feasible

**Coverage:**
```bash
~/.venv/conan/bin/conan install . --build missing -o coverage=True
~/.venv/conan/bin/conan build .
```

## Project Structure

```
include/sisl/          # Public headers (installed downstream)
├── auth_manager/      # Token/auth client interfaces
├── cache/             # range_hashmap, simple_hashmap
├── fds/               # buffer, bitset, stream_tracker, freelist_allocator
├── file_watcher/
├── flip/              # Fault injection
├── grpc/              # rpc_client, rpc_server, generic_service
├── logging/           # logging.h, stacktrace
├── metrics/           # MetricsGroup, counters, gauges, histograms
├── options/           # cxxopts wrapper
├── settings/          # flatbuffers-backed config
├── sobject/           # Smart object lifecycle
├── utility/           # enum, obj_life_counter, cast helpers
└── wisr/              # Wait-free single-reader structures

src/                   # Implementations + tests
3rd_party/             # Custom conan recipes (userspace-rcu)
cmake/                 # CMake helpers (settings_gen.cmake)
```

## Key Types

- `sisl::io_blob` — byte buffer with alignment awareness
- `io_blob_list_t` — `boost::container::small_vector<io_blob, 4>`
- `sg_list` / `sg_iovs_t` — scatter-gather I/O list
- `Result<T>` — `std::expected<T, grpc::Status>` (gRPC layer)
- `AsyncResult<T>` — `std::future<Result<T>>` (gRPC async calls)

## Dependencies

**Always present:** boost, spdlog, nlohmann_json, cxxopts, zmarok-semver, lz4
**metrics=True:** flatbuffers, prometheus-cpp, snappy, userspace-rcu
**grpc=True:** grpc (→ protobuf, abseil), boost
**Optional:** gperftools (tcmalloc), jemalloc, breakpad (Linux/GCC only)

Note: folly has been removed as a dependency (v14.0.0). `folly::SharedMutex` remains in `cache/` and `fds/` headers pending a perf-aware replacement decision.

## Git Workflow

- Main branch: `master`
- Active dev branch: `dev/v14.x` (C++23 + folly removal)
- PR-based workflow; delete `V14_MIGRATION_PLAN.md` before merging `dev/v14.x`

## Code Review

When asked to review a PR, use `gh pr view` and `gh pr diff`, then work through:

### Questions to always ask
- Does this code break anything?
- Are there race conditions or data races?
- Are there logic issues or missed edge cases?
- Could this be smaller or clearer without losing correctness?
- Is this component designed correctly, or are concerns coupled that shouldn't be?
- Are public API changes justified and documented?

### How to post comments
- Post on **specific diff lines**, not as top-level PR comments
- Discuss critical findings with the user before posting
- Always include a suggested fix when raising a bug
- Don't approve or reject PRs yourself
