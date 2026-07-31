# Forge Core Architecture Specification

## Component

**Core / Thread (Thread, ThreadPool, ErasedCallable)**

**Status:** Approved (Architecture Frozen)

---

# Purpose

`Thread` runs an arbitrary no-argument callable on a new OS thread.
`ThreadPool` is a fixed-size set of worker threads pulling submitted
work off a shared queue, for callers that want to hand off many small
tasks without paying OS thread-creation cost per task. `ErasedCallable`
(an implementation detail, not part of either's public API) is the
type-erasure helper both use to get an arbitrary `Callable` across an
OS API boundary that only accepts a fixed `void(*)(void*)` shape.

---

# Responsibilities

* `Thread`: spawn one OS thread running one callable to completion;
  `Join`/`Detach`/`Joinable`.
* `ThreadPool`: spawn N worker threads up front; accept `Submit(callable)`
  calls from any thread and run each callable on whichever worker becomes
  free; `Shutdown` drains whatever is queued and joins every worker.
* `ErasedCallable` (`detail` namespace): allocate a closure holding an
  arbitrary `Callable` via a caller-supplied `memory::Allocator`, and
  hand back a pair of plain function pointers (`invoke` — runs the
  callable then frees the closure; `destroy` — frees it without running
  it, for cleanup-on-failure paths) so `Thread`/`ThreadPool` never need
  to instantiate an OS-facing function per `Callable` type.

---

# Non-Goals

* Cancellation/interruption of a running thread or task. Neither Win32
  nor any sane design lets you safely force-stop arbitrary running code;
  a callable that needs to stop early must poll something (an atomic
  flag, a `stopping_`-style check) itself. `ThreadPool::Shutdown` only
  stops *picking up new* tasks once the queue drains — it never
  interrupts a task already running.
* A general-purpose future/promise or async-result mechanism. A
  submitted `ThreadPool` task's return value (if any) is discarded by
  design here — a caller that needs a result back communicates it
  itself (e.g. writing into a variable it captured by reference, guarded
  by its own `Mutex`, exactly like `ThreadingSmokeTest.cpp`'s
  `RunThreadPoolTest` does). Wiring up a proper future/promise type is
  future work for whenever a concrete caller needs one.
* Growing/shrinking a `ThreadPool`'s worker count after `Initialize()`.
  The size is fixed for the pool's lifetime; a caller wanting elasticity
  can run multiple pools or add that later against a real use case.
* Any platform other than Windows — `Thread` wraps `CreateThread`/
  `WaitForSingleObject`, matching every other OS-facing forge-core
  component's Win32-only precedent.
* An automatic "helpfully detach or join for you" destructor for
  `Thread`. See Design Goals — this is deliberate, not an oversight.

---

# Design Goals

* `Result<T>`/`Result<void>` everywhere OS calls can fail.
* No exceptions.
* Move-only `Thread`, matching `File`/`UniquePtr` — an OS thread handle
  has no well-defined copy operation.
* Every `Thread` that was ever successfully created MUST be `Join()`'d
  or `Detach()`'d before destruction or move-assignment-over — enforced
  with `FORGE_ASSERT`, the same contract (and the same enforcement
  philosophy) `std::thread` has. Silently detaching or silently blocking
  to join on destruction would both hide a real bug (the caller never
  decided what should happen to this thread) instead of surfacing it —
  see `IocpLoop.cpp`'s own `FORGE_ASSERT(port_ != nullptr)`-style
  precedent for "assert on programmer error, don't paper over it".
* `ThreadPool` is **non-movable**, unlike `Thread` — every worker's loop
  captures a pointer back to the pool itself, so the pool needs a stable
  address from the moment the first worker spawns. This is why
  `ThreadPool` follows `IocpLoop`'s own "default-construct, then
  `Initialize()`" shape rather than a `static Create()` factory
  returning the pool by value (which would require it to be movable to
  relocate the return value out of the factory — exactly what a running
  pool cannot safely support). See `ThreadPool`'s own class-comment in
  `ThreadPool.h` for the full reasoning.
* `ErasedCallable`'s allocation and the small `ThreadStart`/`Task` glue
  structs it produces all go through `memory::Allocator` — never raw
  `new`/`delete`, per the project-wide rule (see `PROJECT_CONTEXT.md` →
  "Memory").
* Every `ErasedCallable` produced by `MakeErasedCallable` must have
  exactly one of `invoke`/`destroy` called on it exactly once — both
  `Thread::Create` and `ThreadPool::Submit` guarantee this on every code
  path, including every OOM/failure branch, so a `Callable`'s closure is
  never leaked and never double-freed.

---

# Public API

## ErasedCallable (detail, not user-facing)

```cpp
namespace forge::core::detail
{
struct ErasedCallable
{
    void* closure;
    void (*invoke)(void*) noexcept;  // runs the callable, then frees the closure
    void (*destroy)(void*) noexcept; // frees the closure WITHOUT running it
};

template<typename Callable>
[[nodiscard]] Result<ErasedCallable> MakeErasedCallable(
    memory::Allocator& allocator, Callable callable);
}
```

## Thread

```cpp
class Thread
{
public:
    Thread() noexcept;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&& other) noexcept;
    Thread& operator=(Thread&& other) noexcept;
    ~Thread() noexcept; // FORGE_ASSERT(!Joinable())

    template<typename Callable>
    [[nodiscard]] static Result<Thread> Create(memory::Allocator& allocator, Callable callable);
    template<typename Callable>
    [[nodiscard]] static Result<Thread> Create(Callable callable);

    [[nodiscard]] bool Joinable() const noexcept;
    Result<void> Join() noexcept;
    void Detach() noexcept;

private:
    void* handle_{ nullptr }; // opaque Win32 HANDLE
};
```

## ThreadPool

```cpp
class ThreadPool
{
public:
    ThreadPool() noexcept;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
    ~ThreadPool() noexcept; // calls Shutdown()

    [[nodiscard]] Result<void> Initialize(memory::Allocator& allocator, Size threadCount);
    [[nodiscard]] Result<void> Initialize(Size threadCount);

    template<typename Callable>
    Result<void> Submit(Callable callable);

    void Shutdown() noexcept;
    [[nodiscard]] Size ThreadCount() const noexcept;
    [[nodiscard]] bool Initialized() const noexcept;

private:
    struct Task { void* closure; void (*invoke)(void*) noexcept; };
    Vector<Thread> workers_;
    Queue<Task> tasks_;
    Mutex mutex_;
    ConditionVariable wakeWorker_;
    bool stopping_{ false };
    memory::Allocator* allocator_{ nullptr };
};
```

---

# Memory Layout

`Thread`: a single opaque `void*` (Win32 `HANDLE`). `ThreadPool`: a
`Vector<Thread>` of workers, a `Queue<Task>` of pending work (each
`Task` is two pointers — a closure and its invoke function), a `Mutex`
+ `ConditionVariable` protecting both, and a `bool` stop flag.

---

# Ownership

`Thread` owns exactly one OS thread handle (or none). `ThreadPool` owns
its worker `Thread`s and the allocator-backed closures of every task
currently queued (a submitted-but-not-yet-run task's closure is owned by
the pool's queue until a worker pops and invokes it). Every
`ErasedCallable` closure has exactly one owner at a time, transferred by
plain pointer handoff (never shared/refcounted) from
`Thread::Create`/`ThreadPool::Submit` to whichever thread eventually
calls `invoke`/`destroy` on it.

---

# Error Handling Policy

Every fallible method returns `Result<T>`/`Result<void>`. `Thread::Join`
on a non-joinable `Thread` returns `ErrorCode::InvalidOperation` rather
than asserting (unlike the destructor/move-assignment case) — a caller
that checks `Joinable()` first and branches is behaving reasonably, so
this isn't necessarily a programming error the way an un-joined
`Thread` going out of scope is. `ThreadPool::Submit` after `Shutdown()`
(or before `Initialize()`) likewise returns `ErrorCode::InvalidOperation`
rather than asserting, for the same reason.

---

# Thread Safety

`ThreadPool::Submit`/`Shutdown` are safe to call concurrently from any
number of threads — both take `mutex_` internally. A single `Thread`
instance is not safe to `Join()`/`Detach()` concurrently from two
threads at once (matches every other forge-core type's "external
synchronization is the caller's job" default), though the spawned OS
thread itself obviously runs independently and concurrently with
everything else by design.

---

# Dependencies

Allowed dependencies:

* Core/Types, Core/Error, Core/Result, Core/Assert, Core/Construct
* Core/Sync (`Mutex`, `ConditionVariable`, `LockGuard`) — `ThreadPool`
  only; `Thread` itself does not depend on `Sync.md`'s components.
* Core/Queue, memory/Vector, memory/Allocator, memory/DefaultAllocator
  — `ThreadPool` only.
* `platform/Win32Error.h` (shared Win32 error translation).
* `<windows.h>` (`Thread.cpp` only — never `Thread.h`/`ThreadPool.h`;
  `ThreadPool.cpp` itself has no direct `<windows.h>` dependency at all,
  since it's built entirely on `Mutex`/`ConditionVariable`/`Thread`'s
  own abstractions — see `ThreadPool.cpp`'s own top-of-file comment).

Forbidden dependencies:

* `forge-core/platform/IoLoop`/`IocpLoop` — a thread pool for
  CPU-bound/blocking work is deliberately independent of the single-
  threaded event loop; wiring the two together (e.g. posting a
  completion back onto the loop when a pool task finishes) is Runtime
  Integration's job, not this component's.

---

# Extensibility

Future additions may include:

* A future/promise result type for `ThreadPool::Submit`, once a concrete
  caller needs one (see Non-Goals).
* Shared/reader-mode locking exposed up through anything built on
  `Mutex` (see `Sync.md`'s own Extensibility).
* A POSIX backend, if/when Forge targets non-Windows.
* Work-stealing or per-thread queues in `ThreadPool`, if the single
  shared `Queue<Task>` + one `Mutex` ever shows up as a real contention
  bottleneck — not attempted now per "readability over cleverness"
  without a measured need.

Future additions must **not** introduce:

* Exceptions.
* A `<windows.h>` include in `Thread.h`/`ThreadPool.h`.
* Silent cancellation/interruption of running work (see Non-Goals).

---

# Acceptance Criteria

* Public API implemented exactly as specified.
* `ErasedCallable`/`MakeErasedCallable` — pure logic, zero OS
  dependency — **fully verified**: `g++`/`clang++ -std=c++17
  -fno-exceptions -Wall -Wextra -Wpedantic -Werror`, ASan+UBSan,
  `valgrind --leak-check=full`, `-O2` (`ErasedCallableTest.cpp`),
  covering invoke-runs-and-frees, destroy-without-invoking-never-runs,
  a move-only capture, and an OOM path via `FailingAllocator`.
* `Thread`/`ThreadPool` — **cannot be compiled or run in this sandbox**
  (no Windows SDK, no working MinGW cross-compiler; same constraint as
  `File.md`/`Sync.md`). Verified by careful manual review plus a
  hand-written mock `<windows.h>` compile-AND-link pass under the full
  `-std=c++17 -fno-exceptions -Wall -Wextra -Wpedantic -Werror` bar on
  both compilers — including forcing every template
  (`Thread::Create<Callable>`, `ThreadPool::Submit<Callable>`,
  `MakeErasedCallable<Callable>`) to actually instantiate, not just
  compiling each `.cpp` in isolation. The mock-linked binary was also
  run clean under ASan+UBSan and `valgrind --leak-check=full` (0 leaks)
  — this exercises the real allocation/cleanup logic even though the
  mock's `CreateThread`/`WaitForSingleObject` are dumb stubs that make
  every `Thread::Create` call "fail" in that environment. This must be
  called out explicitly wherever `Thread`/`ThreadPool` are described as
  "done" — implemented, not confirmed, until a real `mach build`
  compiles them and `ThreadingSmokeTest.cpp` runs clean on the actual
  machine.

---

# Implementation Status

* Architecture: Approved
* Public API: Frozen
* Header/Implementation: Complete (`ErasedCallable.h`, `Thread.h/.inl/.cpp`,
  `ThreadPool.h/.inl/.cpp`)
* Tests: `ErasedCallableTest.cpp` — sandbox-verified, all scenarios
  passing. `ThreadingSmokeTest.cpp` — real-machine only (Mutex mutual-
  exclusion stress test across 8 threads × 20000 increments each,
  ConditionVariable producer/consumer handoff, `WaitFor` timeout,
  `ThreadPool` with 4 workers running 500 submitted tasks and verifying
  every one ran exactly once, plus `Submit` after `Shutdown()` correctly
  failing) — not yet run; needs the real machine.
