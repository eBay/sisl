# sisl 14.0.0 migration plan — handoff notes

**Status**: work-in-progress on branch `dev/v14.x`. This file is a transient planning doc — delete before merging to master.

## Why 14.0.0 (the narrative)

The pass started as "port sisl to clang-22" and expanded into a coordinated major bump because multiple forcing functions arrived together:

- **C++23 requirement** — other projects in this ecosystem are already on C++23; sisl lagging at C++20 was a compat tax.
- **Clang-22 / libc++ compatibility** — original trigger. grpc 1.54.3 + abseil 20230802 hit real correctness issues under newer clang/libc++.
- **Breakpad is semi-abandoned** — `cci.20210521` recipe is ~4 years stale; libc++ catches a real unique_ptr<incomplete> bug that libstdc++ tolerates.
- **Folly is a millstone** — custom recipe we maintain, 15+ transitive deps (libdwarf, libiberty, libevent, libsodium, xz_utils, double-conversion, zstd, etc.), ongoing libc++ friction.

User's strategic priority: **bundle all SemVer-major-worthy changes into 14.0.0** to avoid cutting 15.0.0 immediately after. Downstream consumers pay the coordination cost once.

## Recipe changes already made (committed on this branch)

### `conanfile.py`

- `spdlog/1.14.1` → `spdlog/1.17.0`
- `prometheus-cpp/1.1.0` → `prometheus-cpp/1.3.0`
- `grpc/1.54.3` → `grpc/1.69.0` (this pulls protobuf 3.21 → ~5.x, abseil refresh)
- `folly` pin updated to `nu2.2024.08.12.00.1` (matches recipe)
- `benchmark/1.9.4` → `benchmark/1.9.5` (fixes clang-22 `-Wc2y-extensions` on `__COUNTER__`)
- Breakpad guarded off on clang via `if self.settings.os in ["Linux"] and self.settings.compiler != "clang"` — **stopgap, see section below**
- Protoc comment updated to reflect grpc 1.69

### `3rd_party/folly/conanfile.py`

- `fmt/10.2.1` → `fmt/12.1.0`
- `liburing/2.6` → `liburing/2.11`
- Dropped `patches/2024-001-timespec.patch` (liburing 2.11 has the fix upstream)

### `3rd_party/folly/conandata.yml`

- Patches section removed (no patches needed anymore)

### `src/logging/` (minimal source surgery for breakpad stopgap)

- `src/logging/stacktrace_release.h`: `#if defined(__linux__)` → `#if defined(__linux__) && defined(SISL_HAS_BREAKPAD)` (three places)
- `src/logging/CMakeLists.txt`: added `target_compile_definitions(sisl_logging PRIVATE SISL_HAS_BREAKPAD)` guarded on `breakpad_FOUND`

### NOT yet changed but intended

- **`_min_cppstd` is still 20** in `conanfile.py`. Needs to become 23. Verify before moving on.
- **`version` is still 13.2.5** in `conanfile.py`. Needs to become 14.0.0 when we tag. Don't bump until the build is actually clean on both toolchains — keeping it at 13.2.5 during WIP avoids polluting conan caches.

## Profile / tooling setup (not committed — lives in ~/.conan2/profiles/)

Current state of personal profiles on the old machine:

### `clang` profile
```
[settings]
arch=x86_64
build_type=Debug
compiler=clang
compiler.cppstd=gnu23
compiler.libcxx=libc++     # <-- debatable, see open questions
compiler.version=22
os=Linux
[conf]
tools.build:compiler_executables={"c": "/usr/bin/clang", "cpp": "/usr/bin/clang++"}
```

### `default` profile
```
[settings]
compiler=gcc
compiler.cppstd=20          # should be gnu23 eventually
compiler.libcxx=libstdc++11
compiler.version=15
os=Linux
[platform_tool_requires]
m4/1.4.19
```

The `[platform_tool_requires] m4/1.4.19` is critical: m4/1.4.19's bundled gnulib fails under clang-22 due to misplaced `[[nodiscard]]`; Arch has system m4/1.4.21 which is fine. This tells conan to use the system m4 instead of building its own.

**User discussed committing profile templates under `ci/profiles/` in the repo**, so CI and other devs don't rely on personal conan config. Not yet done — may be worth doing as part of 14.0.0.

## Stopgaps that MUST unwind before tagging 14.0.0 final

### 1. Breakpad → boost::stacktrace (DEFERRED SOURCE WORK)

The current state: breakpad is silently dropped on clang via a compiler guard. That means **clang builds have no crash dump capability** — acceptable during "does it build" exploration, unacceptable for production.

The plan (deferred so user could focus on recipe-level build validation):

1. Drop remaining breakpad conditionals in `conanfile.py`
2. Replace `breakpad::ExceptionHandler::WriteMinidump(...)` in `src/logging/stacktrace_release.h` with `boost::stacktrace::safe_dump_to("/path/file.dump")`
3. Replace the next-boot minidump-parse path (wherever that lives — grep for `MinidumpDescriptor` or similar) with `boost::stacktrace::stacktrace::from_dump("/path/file.dump")` + stream into log
4. Add `BOOST_STACKTRACE_USE_BACKTRACE` compile define on the `logging` component (default backend just prints hex addresses, useless — need libbacktrace backend)
5. Test: actually trigger SIGSEGV on both gcc+clang and confirm dump→parse round-trip produces readable stack traces

Why boost::stacktrace specifically (vs cpptrace / std::stacktrace):
- boost::stacktrace has `safe_dump_to` + `from_dump` pair that matches sisl's exact existing workflow (signal-safe binary dump at crash, parse to text at next boot)
- boost is already a direct dep — zero new graph additions
- `std::stacktrace` (C++23) can't do the dump/parse-later pattern; its capture isn't guaranteed async-signal-safe
- cpptrace is nice but would need custom serialization for the dump/parse workflow

**This is a SemVer major change regardless** — breakpad is in the public `requires` list of the `logging` component, so removing it breaks any downstream transitively linking it. Has to ship in 14.0.0 or be cut to 15.0.0 (which user wants to avoid).

## Open work — where we were when switching machines

### Current blocker: folly fails to build on clang-22/libc++

```
error: implicit instantiation of undefined template 'std::char_traits<unsigned char>'
```

Folly's `Range.h` uses `std::basic_string_view<unsigned char>` (or similar) which relies on a non-standard `std::char_traits<unsigned char>` specialization. libstdc++ still provides it; libc++ removed it. This is folly's problem, not the compiler's.

### Decision taken: remove folly entirely

User proposed removing folly as a dep rather than continuing to wrestle with it. Investigation showed folly usage in sisl is **remarkably contained**:

| Symbol | Usages | Replacement |
|---|---|---|
| `folly::SharedMutex` | 23 | `std::shared_mutex` |
| `folly::Promise<T>` / `makePromiseContract` | 8 | `std::promise<T>` + `get_future()` |
| `folly::Unit` | 4 | empty struct / `std::monostate` |
| `folly::small_vector<T>` | 4 (+1 policy) | `boost::container::small_vector` |
| `folly::makeUnexpected` / `Expected` | 3 | `std::unexpected` / `std::expected` (C++23) |
| `folly::ThreadLocalPtr<T>` | 1 | `thread_local std::unique_ptr<T>` (inspect usage first) |
| `folly::Synchronized<T>` | 1 | small `std::mutex` wrapper |
| `folly::SemiFuture<T>` | 1 | `std::future<T>` |

**Only 11 files touched.** None of the hard folly features (`Future.then()`, `F14Map`, `fbstring`, `dynamic`, `ConcurrentHashMap`, `hazptr`, `IndexedMemPool`) are used.

### Per-swap performance analysis

| Swap | Runtime cost delta | Severity |
|---|---|---|
| `folly::SharedMutex` → `std::shared_mutex` | Real, measurable under high reader contention (folly 3-5× faster at 16+ readers; at low contention within 10-20%) | **The only real concern** — 23 sites |
| `folly::Promise`/`SemiFuture` → `std::promise`/`std::future` | Essentially none; both heap-allocate shared state, both use futex waiters | Negligible |
| `folly::Synchronized<T>` → std::mutex wrapper | Depends on whether usage is read-dominated or balanced | Minor — single usage, inspect |
| `folly::small_vector` → `boost::container::small_vector` | Within noise; boost is slightly less memory-compact in some configs | Negligible |
| `folly::Unit` → empty struct | Zero — compile-time marker | None |
| `folly::Expected` / `makeUnexpected` → `std::expected` | Zero when T/E are small; aligns with C++23 direction | None |
| `folly::ThreadLocalPtr<T>` → `thread_local std::unique_ptr<T>` | Native `thread_local` is faster (single `mov` from fs/gs). Folly has teardown/NUMA-awareness features — inspect the 1 usage | Semantic concern, not perf |

### Recommended folly removal execution plan

1. **Inspect the single `folly::ThreadLocalPtr` usage** first — check if it relies on folly's registry/teardown semantics or if it's a simple per-thread store. Determines whether `thread_local std::unique_ptr<T>` is semantically adequate.
2. **Do the swap as `std::shared_mutex` across the board** for the SharedMutex sites. Don't preemptively optimize.
3. **Benchmark the 2-3 hottest-looking SharedMutex sites** after swap with synthetic reader-contention load. Suspected hot locations: buffer/cache/metrics paths.
4. **If regression is measurable**: escalate those specific files to `absl::Mutex` reader-writer mode. Abseil is already transitively in the graph via grpc; adding a direct dep is cheap. **Don't reach back for folly** — the whole point is to remove it.
5. **Other swaps are essentially free** — do them mechanically without ceremony.
6. **Drop folly from `conanfile.py`**: remove from requires, remove from cpp_info.components, and delete `3rd_party/folly/` recipe directory.
7. **Graph cleanup**: libdwarf, libiberty, libevent, libsodium, xz_utils, double-conversion, zstd, glog, gflags may fall out of the transitive graph entirely. fmt needs inspection — spdlog may pull it independently.
8. **Retry the clang+libc++ build** — the char_traits<unsigned char> issue should be gone entirely since it was folly's code.

## Decisions still pending

1. **libc++ vs libstdc++ on Linux clang**: currently libc++, which has been the source of most build friction (breakpad, grpc, folly all failed under libc++'s strict mode). After folly is gone, worth re-testing libc++ to see if it's clean. If still painful, switching to `libstdc++11` is a pragmatic call — most Linux clang production CIs use libstdc++ anyway. **Caveat**: macOS is libc++-only (apple-clang), so if macOS UT support is a real story, libc++ on Linux clang makes the platforms consistent.

2. **macOS UT support**: user mentioned wanting to build and run UTs on Mac. Not yet tested in this pass. If that story is real, need to keep libc++ everywhere and ensure all deps work under libc++.

3. **Profile templates in repo**: user expressed concern about profile changes being developer-local and not distributed. `ci/profiles/clang` etc. committed to the repo would solve this. Not yet done.

4. **Version bump timing**: when do we actually change `version = "13.2.5"` → `"14.0.0"` in conanfile.py? Probably only once the build is actually green on both toolchains, to avoid polluting conan caches with broken 14.0.0 packages.

## Known verified-working state

- **Graph resolution**: clean under `-pr:h clang -pr:b default`
- **Tool builds that succeeded**: benchmark/1.9.5, m4/1.4.21 (system), protoc (both gcc and clang-libc++ contexts), some earlier grpc/1.54 compilation started succeeding before we bumped
- **Not yet verified**: full clang build end-to-end at grpc/1.69; gcc build end-to-end at new versions; any runtime testing

## Quick pickup checklist for new machine

1. `git checkout dev/v14.x && git pull`
2. Ensure Arch (or equivalent) has m4 ≥ 1.4.20 system-installed: `m4 --version`
3. Mirror the two conan profiles: `~/.conan2/profiles/default` and `~/.conan2/profiles/clang` (see "Profile / tooling setup" section above — not in repo)
4. `conan install . -pr:h clang --build missing` — should resolve the graph and start building deps
5. Start folly removal work — smallest-risk first is probably the single `Unit`/`Synchronized`/`SemiFuture`/`ThreadLocalPtr` usages to get muscle memory, then the 8 Promise/Future sites, then the 23 SharedMutex sites, then delete the folly recipe + requires
6. Re-run clang build to verify char_traits issue is gone
7. Then come back to the breakpad→boost::stacktrace swap
8. Then bump `version` and `_min_cppstd`, tag 14.0.0

## File locations reference

- `/home/ne1k0/dev/oss/sisl/conanfile.py` — main recipe (breakpad guards at lines ~84 and ~224)
- `/home/ne1k0/dev/oss/sisl/3rd_party/folly/` — custom folly recipe (to be deleted during folly removal)
- `/home/ne1k0/dev/oss/sisl/src/logging/stacktrace_release.h` — breakpad stopgap source guards
- `/home/ne1k0/dev/oss/sisl/src/logging/CMakeLists.txt` — `SISL_HAS_BREAKPAD` wiring

## Memory notes

The previous machine saved a project memory at `~/.claude/projects/-home-ne1k0-dev-oss-sisl/memory/project_breakpad_swap_pending.md` with the breakpad→boost::stacktrace deferral context. Claude's memory is per-machine, so the new machine won't have it — but this document captures the same content. If you want Claude on the new machine to persist it, re-save equivalent memory there.
