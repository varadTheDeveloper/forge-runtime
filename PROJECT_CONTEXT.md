# Forge Core

## Source of truth

`C:\spidermonkey-dev\gecko-dev\js\src\forge\` is the **only** tracked
copy of this project as of 2026-07-29 — it's the real Gecko/SpiderMonkey
tree `mach build` compiles from. `C:\forge-beta\forge-runtime-beta\`
(the original working copy) is retired; do not develop in or sync
changes to it unless explicitly asked to. This file, `HISTORY.md`,
`ROADMAP.md`, and `AGENTS.md` all now live directly under
`js\src\forge\` (not `.claude\`, which rejects remote writes) —
alongside `forge-core/`, `forge.cpp`, and `moz.build`.

## Vision

Forge Core is a modern C++ systems library being built as the foundation of
the Forge JavaScript Runtime. Written to compile as C++17, not C++20 — the
real build (`mach build` inside the Gecko/SpiderMonkey tree) rejects
C++20-only constructs; see `AGENTS.md` → "Coding Standards" and
`HISTORY.md` → "Real build environment discovered" for what that ruled
out and why.

The goal is to build a production-quality runtime that can eventually
compete with Node.js, Deno, and Bun.

Forge Core is the low-level platform library that provides memory,
containers, filesystem, threading, synchronization, networking and platform
abstractions. The runtime itself (`forge/forge.cpp`, `forge/main.cpp`) embeds
SpiderMonkey and is expected to be rebuilt on top of Forge Core over time,
rather than using raw `std::` containers and the event-loop code currently
prototyped there.

Everything must be production quality.

---

# Philosophy

- Zero exceptions
- Explicit error handling
- Cross-platform
- RAII
- UTF-8 internally
- Small, clean API
- No unnecessary dependencies
- Performance first
- Readability over cleverness

---

# Development Rules

Every component follows this workflow.

1. Design API
2. Review API
3. Freeze API
4. Implement in small parts
5. Review implementation
6. Freeze implementation

Once frozen, APIs are NOT redesigned except for:

- Bugs
- Security issues
- Incorrect behaviour
- Fundamental design flaws

Never redesign frozen components. See `HISTORY.md` for what is already
frozen before touching any existing header. See `AGENTS.md` for how an AI
agent specifically should approach a task in this repo (review checklist,
"be honest about compiling", the include/naming pitfalls already hit
once).

---

# Error Handling

Constructors never fail.

Functions that may fail return `Result<T>` (or `ResultVoid` for functions
with no value to return).

Objects requiring allocation during creation use a static factory:

```
Create(...)
```

Example: `String::Create(...)` instead of `String(...)`.

`Error` is a small value type (`ErrorCode` + native platform code), storing
no message, no stack trace, no formatting. It never allocates and never
throws. See `forge/forge-core/Error.md` for the full frozen spec and
`forge/forge-core/Error.h` for the implementation.

`Failure` exists purely to disambiguate constructing a failed `Result<T>`
from a `Result<T>` that legitimately holds an `Error` as its value type.

---

# Memory

All allocations go through `memory::Allocator`.

Never use:

- `new`
- `delete`
- `malloc`
- `free`

Allocator contract:

- `Allocate(size, alignment)` returns `nullptr` on failure.
- `Deallocate(memory, size, alignment)` must receive the same size/alignment
  used during the matching `Allocate()` call.

`DefaultAllocator` is the current concrete allocator. `UniquePtr`/`MakeUnique`
and `Vector` are built on top of the `Allocator` interface, not on
`std::allocator`.

---

# Current Progress

Completed (API + implementation present in `forge/forge-core/`):

- Types (`Types.h` — fixed-width integer/float aliases, `Size`, `Offset`, `Byte`)
- Error (`Error.h`, spec in `Error.md`)
- Failure (`Failure.h`)
- Result / ResultVoid (`Result.h`, `Result.inl`, `ResultStorage.h`,
  `ResultVoid.h`, `ResultVoid.inl`, `ResultFwd.h`)
- Assert (`Assert.h`)
- Allocator / DefaultAllocator (`memory/Allocator.h`,
  `memory/DefaultAllocator.h/.cpp`, `memory/detail/AllocationBackend.h/.cpp`)
- UniquePtr / MakeUnique (`memory/UniquePtr.h/.inl`, `memory/MakeUnique.h/.inl`)
- Vector (`memory/Vector.h/.inl`)
- String / StringView / Span (`Span.h`, `StringView.h`, `String.h/.inl`)
- Array / Stack / Queue / Hash / HashMap / HashSet (`Array.h`, `Stack.h/.inl`,
  `Queue.h/.inl`, `Hash.h`, `HashMap.h/.inl`, `HashSet.h/.inl`)
- Path (`Path.h/.inl`, spec in `Path.md`) — fully verified, no OS dependency
- File (`File.h/.cpp`, spec in `File.md`) — **done, real-build-confirmed**.
  A real `mach build` on 2026-07-29 caught a genuine bug
  (`CreateDirectory` colliding with `<windows.h>`'s
  `CreateDirectory`→`CreateDirectoryA`/`W` macro), fixed by renaming the
  method to `MakeDirectory` — see `HISTORY.md`. A second real `mach
  build` afterward compiled clean, confirming the fix. Phase 3 is
  complete.
- Sync — Mutex / ConditionVariable / LockGuard (`Mutex.h/.cpp`,
  `ConditionVariable.h/.cpp`, `LockGuard.h`, spec in `Sync.md`) —
  `LockGuard` fully verified (pure logic, no OS dependency); `Mutex`/
  `ConditionVariable` **implemented but not yet confirmed** — Win32-only,
  could not be compiled in this sandbox (no Windows SDK / working MinGW
  available), verified by manual review plus a mock-`<windows.h>`
  compile+link pass only.
- Thread / ThreadPool / ErasedCallable (`Thread.h/.inl/.cpp`,
  `ThreadPool.h/.inl/.cpp`, `ErasedCallable.h`, spec in `Thread.md`) —
  `ErasedCallable` fully verified (pure logic, no OS dependency);
  `Thread`/`ThreadPool` **implemented but not yet confirmed**, same
  Win32-only constraint as `Mutex`/`ConditionVariable`/`File`. Needs a
  real `mach build` (or a standalone Visual Studio run of
  `ThreadingSmokeTest.cpp`) before these can say "verified" the way
  everything else here can.

All of the above compiled cleanly and passed a runtime test (including
allocator-failure paths) as of 2026-07-26 — see `HISTORY.md` for the list
of bugs that pass fixed. Before that date this had never actually been
compiled end-to-end. String/StringView/Span were added and verified
2026-07-27, Array/Stack/Queue/Hash/HashMap/HashSet 2026-07-29, Path/File
2026-07-29 (File real-build-confirmed the same day after a rename fix),
and Sync/Thread/ThreadPool 2026-07-29, all under the corrected
C++17/no-exceptions constraints (see `AGENTS.md`) rather than the
earlier, wrong C++20 assumption.

`forge/platform/` (Windows path handling groundwork) existed earlier in
the project but was deliberately removed; the filesystem layer (Phase 3)
was designed from scratch rather than building on it, starting from two
frozen spec docs (`Path.md`, `File.md`) per this phase's own requirement.
See `HISTORY.md`.

- Runtime integration (`forge.cpp`) — **done, real-build-confirmed and
  benchmarked**, corrected 2026-07-30. `forge.cpp`'s microtask queue and
  timer registry now use `Queue<memory::UniquePtr<Microtask>>` and
  `HashMap<int, memory::UniquePtr<JsTimer>>` instead of
  `std::vector`/`std::unique_ptr`, script loading uses `Path`/`File`
  instead of `std::ifstream`/`std::stringstream`, and
  `EnqueueMicrotask`/`SetTimeout`/`SetInterval` route allocation through
  `memory::MakeUnique<T>` instead of `std::make_unique`. Sandbox
  verification compiled every container/allocator call used against the
  real `forge-core` headers (not mocks) under g++/clang++ with full
  warnings, clean under ASan+UBSan and valgrind. The user then built this
  exact `forge.cpp` with a real `mach build` and ran the full
  `bench/run-benchmarks.ps1` suite against it (2026-07-30) — see
  `ROADMAP.md`'s Phase 6 entry and `Forge_Benchmark_Report.md` for the
  full same-machine Forge/Bun/Node results across all six benchmark
  scripts, and for the one still-open item (a `promise-chain-bench.js`
  performance gap not yet explained by profiling — kept separate from
  this "done" status since it's follow-up work, not a blocker). See
  `HISTORY.md`'s "Phase 6 corrected"/"Phase 6 benchmarked" entries: an
  earlier session had reported this same work as delivered and confirmed
  by a real `mach build`, but the actual `forge.cpp` on disk never
  contained these changes at the time — that report did not reflect
  reality; this entry reflects what has now actually been verified.

- Filesystem JS API, Phase 7.2 (JS/native marshalling primitives in
  `forge.cpp`) — **fully real-build-confirmed** (2026-07-30). The six
  helpers themselves, a follow-up `forge --self-test` smoke test suite
  (12 cases, exercising all six against a live `JSContext`/`Realm`), and
  a linkage cleanup have all now been through a real `python mach build`,
  with `forge --self-test` printing `[self-test] ALL PASSED` (exit code
  0). See `ROADMAP.md`'s Phase 7.2 entry and `HISTORY.md` for the full
  verification account, the two binary-marshalling design decisions
  made, and the `JsBindings.md` behavioral clarification recorded
  alongside it. Phase 7.1 (`JsBindings.md`/`Fs.md`) remains frozen and
  unchanged.

- Filesystem JS API, Phase 7.3 (synchronous `fs.*Sync` bindings) —
  **fully real-build-confirmed** (2026-07-30). All seven methods from
  `Fs.md`'s Public API (`readFileSync` through `statSync`) are wired to
  `globalThis.fs` in `forge.cpp`, and the 17-case addition to
  `forge --self-test` (`RunFsSmokeTests`) exercising every method's
  success and failure path has now been through a real `python mach
  build`, with `forge --self-test` printing `[self-test] ALL PASSED`
  across all 29 cases (the original 12 plus these 17), exit code 0. See
  `ROADMAP.md`'s Phase 7.3 entry and `HISTORY.md` for the full
  verification account (including the sandbox-only compile check and
  direct-native-function invocation work done before this real build, for
  the record).

- Filesystem JS API, Phase 7.4 (benchmarks) — **done** (2026-07-30).
  `bench/fs-bench.js` written and run through the updated
  `run-benchmarks.ps1` against real `forge.exe`/`bun.exe`/`node.exe` on
  the user's machine: Forge/Bun ratio 0.86x, Forge/Node ratio 0.91x
  (Forge faster than both). This run also confirmed, for the first time,
  that the full seven-script bench suite runs clean end-to-end on all
  three runtimes with no failures — including the timer/microtask/
  promise-chain batch that had never been run end-to-end before. See
  `ROADMAP.md`'s Phase 7.4 entry and `HISTORY.md` for the full table.
  Along the way, this file and `ROADMAP.md`/`HISTORY.md`'s Phase 6
  entries were found to reference a `Forge_Benchmark_Report.md` that did
  not actually exist anywhere in the repo (confirmed via a direct
  directory listing) — raised with the user directly rather than
  silently resolved either way; the user asked for a fresh report
  authored from today's real numbers. `Forge_Benchmark_Report.md` now
  exists, dated 2026-07-30, with all seven scripts' results and its own
  Provenance Note documenting the discrepancy rather than presenting
  itself as a recovered original. **Phase 7, all of it (7.1-7.4), is now
  done.**

Not started:

- Networking — `Socket`/`IpAddress` are implemented on disk
  (`forge-core/Socket.h/.cpp`, `IpAddress.h/.inl`) but **`moz.build` does
  not include `Socket.cpp` in `SOURCES`**, so no real build has ever
  actually compiled it in. An earlier session reported this phase as
  real-build-confirmed; that did not reflect what's actually on disk
  either (discovered 2026-07-30 alongside the Runtime Integration
  correction above — see `HISTORY.md`). Needs `moz.build` updated, a real
  `mach build`, and this section updated once that's actually done.

See `ROADMAP.md` for the intended order of the remaining work, and
`HISTORY.md` for decisions already frozen on the completed components.

---

# Coding Style

- Header-only implementations use `.inl`
- Public API in `.h`
- Inline function bodies in `.inl`
- `constexpr` whenever possible
- `noexcept` whenever possible
- `[[nodiscard]]` where appropriate
- Namespace: `forge::core` for core library types
- Includes: relative, directory-correct paths (`"Types.h"`, `"../Error.h"`,
  `"memory/ResultFwd.h"`) — never a `forge/core/...`-style rooted path.
  The physical directory is `forge/forge-core/` (hyphenated), and relative
  includes work regardless of how the surrounding build's include roots
  are configured. See `HISTORY.md` for why this matters (most of the
  codebase used the wrong convention until the 2026-07-26 pass).
- If a member function's name matches a type visible in the enclosing
  namespace (e.g. `Error()` returning `Error&`, `Size()` returning `Size`),
  GCC hard-errors (`-Wchanges-meaning`) rather than just warning. Fix with
  the `class Error` elaborated-type-specifier for real class types, or by
  fully qualifying the alias target (`forge::core::Size`) when the type is
  a `using`-alias. Do not rename the accessor to work around this — see
  `AGENTS.md` for the full explanation.

Component specs (like `Error.md`) follow this shape: Purpose, Responsibilities,
Non-Goals, Design Goals, Public API, Memory Layout, Ownership, Error Handling
Policy, Thread Safety, Dependencies, Extensibility, Acceptance Criteria,
Implementation Status. Write a matching spec doc before freezing any new
component's API.

---

# Long-Term Goal

Forge Core is not intended to be merely another container library.

It is the complete systems foundation for the Forge JavaScript Runtime.

Every design decision should consider:

- Performance
- Maintainability
- Explicit APIs
- Consistency
- Future runtime requirements
