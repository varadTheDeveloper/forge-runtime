# Forge Core Architecture Specification

## Component

**Core / Sync (Mutex, ConditionVariable, LockGuard)**

**Status:** Approved (Architecture Frozen)

---

# Purpose

The basic synchronization primitives every later Phase 4 component
(`Thread`, `ThreadPool`) and, eventually, Runtime Integration's job-queue
handoff between the main JS thread and background work, are built on:
mutual exclusion (`Mutex`), condition signaling (`ConditionVariable`),
and RAII scoped locking (`LockGuard<T>`).

---

# Responsibilities

* `Mutex`: non-recursive mutual exclusion — `Lock`/`Unlock`/`TryLock`.
* `ConditionVariable`: block a thread until notified, always used
  together with a locked `Mutex` — `Wait`/`WaitFor`/`NotifyOne`/
  `NotifyAll`.
* `LockGuard<Lockable>`: RAII scoped lock — locks in its constructor,
  unlocks in its destructor, for any type with `Lock()`/`Unlock()`
  methods (not hardcoded to `Mutex`, so it can be tested against a fake
  Lockable without a real OS lock — see Non-Goals/Acceptance Criteria).

---

# Non-Goals

* Recursive locking. A thread that locks an already-held `Mutex` again
  deadlocks itself rather than silently succeeding — this matches
  SRWLOCK's own native behavior and avoids the recursive-mutex
  antipattern (needing to lock again is usually a sign of a design
  problem worth surfacing, not papering over with reentrancy).
* Reader/writer distinction. Win32's SRWLOCK natively supports a
  shared/reader mode, but nothing in this codebase needs it yet — adding
  `LockShared`/`UnlockShared` later, if a real use case shows up, is
  additive to `Mutex`'s API, not a breaking change.
* A `std::unique_lock`-style movable/deferred-lock wrapper. `LockGuard<T>`
  intentionally only covers the simple "lock now, unlock at scope exit"
  case; `ConditionVariable::Wait` takes a `Mutex&` directly rather than a
  lock wrapper, so callers manage locking explicitly (see `Thread.md`'s
  `ThreadPool` for the pattern this produces in practice).
* Any platform other than Windows — `Mutex`/`ConditionVariable` wrap
  SRWLOCK/CONDITION_VARIABLE, matching every other OS-facing forge-core
  component's Win32-only, hard-`#error`-on-anything-else precedent
  (`IoLoop.h`, `File.md`).

---

# Design Goals

* Never fail to construct or lock. SRWLOCK/CONDITION_VARIABLE need no
  heap allocation and their initialization cannot fail (unlike a Win32
  `CRITICAL_SECTION`, whose `InitializeCriticalSection` can, in
  principle, raise an OS exception under extreme low memory) — this is
  why `Mutex`/`ConditionVariable` have no `Create()` factory, and
  `Lock()`/`Unlock()`/`NotifyOne()`/`NotifyAll()` return `void` rather
  than `Result<void>`. Only genuinely exceptional OS errors from
  `Wait`/`WaitFor` return `Result`.
* Keep `<windows.h>` out of every header. `Mutex`/`ConditionVariable`
  store their native SRWLOCK/CONDITION_VARIABLE behind an opaque `void*`
  — both are documented, ABI-stable, exactly-one-pointer-sized values
  that are valid when zero-initialized (this is literally what
  `SRWLOCK_INIT`/`CONDITION_VARIABLE_INIT` expand to), so a
  zero-initialized `void*` is bit-for-bit equivalent without needing the
  real type in the header. Each `.cpp` `static_assert`s this size/
  alignment assumption. Same reasoning as `File.h`'s `HANDLE`-as-`void*`.
* `LockGuard<T>` is a template on the lockable type, not hardcoded to
  `Mutex`, specifically so its logic (locks in constructor, unlocks in
  destructor, non-copyable/non-movable) can be verified in this sandbox
  against a fake test double, even though `Mutex` itself cannot be.

---

# Public API

## Mutex

```cpp
class Mutex
{
public:
    Mutex() noexcept;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;
    ~Mutex() noexcept = default;

    void Lock() noexcept;
    void Unlock() noexcept;
    [[nodiscard]] bool TryLock() noexcept; // true if acquired, false if already held

private:
    void* native_{ nullptr }; // opaque SRWLOCK
};
```

## ConditionVariable

```cpp
class ConditionVariable
{
public:
    ConditionVariable() noexcept;
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    ConditionVariable(ConditionVariable&&) = delete;
    ConditionVariable& operator=(ConditionVariable&&) = delete;
    ~ConditionVariable() noexcept = default;

    // `mutex` must already be locked by the caller; atomically unlocked
    // while waiting, re-locked before returning. Spurious-wakeup-safe
    // contract: the caller must re-check its own predicate in a loop.
    Result<void> Wait(Mutex& mutex) noexcept;

    // Same contract, gives up after timeoutMs. true = notified,
    // false = timed out (not itself an Error).
    [[nodiscard]] Result<bool> WaitFor(Mutex& mutex, u32 timeoutMs) noexcept;

    void NotifyOne() noexcept;
    void NotifyAll() noexcept;

private:
    void* native_{ nullptr }; // opaque CONDITION_VARIABLE
};
```

## LockGuard\<Lockable\>

```cpp
template<typename Lockable>
class LockGuard
{
public:
    explicit LockGuard(Lockable& lockable) noexcept; // calls lockable.Lock()
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    LockGuard(LockGuard&&) = delete;
    LockGuard& operator=(LockGuard&&) = delete;
    ~LockGuard(); // calls lockable.Unlock()

private:
    Lockable& lockable_;
};
```

---

# Memory Layout

`Mutex`/`ConditionVariable`: a single opaque `void*` each (documented
Win32 ABI: exactly one pointer-sized value). `LockGuard<T>`: a single
reference, no storage of its own.

---

# Ownership

None of these three own a resource in the `UniquePtr`/`File` sense —
`Mutex`/`ConditionVariable` are value types with in-place state (not a
separate heap-allocated handle reached through a pointer), and
`LockGuard<T>` only ever *borrows* a `Lockable&` for its own lifetime,
never owns one. All three are non-copyable and non-movable: relocating a
`Mutex`/`ConditionVariable` after another thread has started
waiting/locking against its current address would be unsafe, and
`LockGuard` moving would create exactly the double-unlock hazard RAII
exists to prevent (see `std::lock_guard`'s identical choice).

---

# Error Handling Policy

`Lock`/`Unlock`/`TryLock`/`NotifyOne`/`NotifyAll` never fail — see
Design Goals. `Wait`/`WaitFor` return `Result<void>`/`Result<bool>` only
for genuinely exceptional OS errors; a timeout from `WaitFor` is
represented as `Result<bool>(false)`, not a `Result` error, since timing
out is an expected, successful outcome of calling a *timed* wait.

---

# Thread Safety

The entire point of this component: `Mutex`/`ConditionVariable` are
designed to be shared across threads and used concurrently — that is
the only way they're useful. `LockGuard<T>` itself is not
thread-shared; each thread that locks a given `Mutex` constructs its
own `LockGuard` around it.

---

# Dependencies

Allowed dependencies:

* Core/Types, Core/Error, Core/Result
* `<windows.h>` (`Mutex.cpp`/`ConditionVariable.cpp` only — never the
  headers)
* `platform/Win32Error.h` (shared `GetLastError()` → `Error` translation,
  also used by `File.cpp` — see `HISTORY.md`'s Phase 4 entry for why
  this was extracted out of `File.cpp` rather than duplicated a second
  time)

Forbidden dependencies:

* Nothing in this component may depend on `Thread`/`ThreadPool`
  (`Thread.md`) — the dependency runs the other way.

---

# Extensibility

Future additions may include:

* Shared/reader-mode locking on `Mutex` (`LockShared`/`TryLockShared`/
  `UnlockShared`), once a concrete reader/writer use case exists.
* A POSIX backend (`pthread_mutex_t`/`pthread_cond_t`), if/when Forge
  targets non-Windows — behind the same `#if defined(_WIN32)` pattern
  `IoLoop.h` already establishes.

Future additions must **not** introduce:

* Exceptions.
* A `<windows.h>` include in `Mutex.h`/`ConditionVariable.h`.
* Recursive locking on `Mutex` as the default behavior.

---

# Acceptance Criteria

* Public API implemented exactly as specified.
* `LockGuard<T>` — pure logic, zero OS dependency — **fully verified**:
  `g++`/`clang++ -std=c++17 -fno-exceptions -Wall -Wextra -Wpedantic
  -Werror`, ASan+UBSan, `valgrind --leak-check=full`, `-O2`, against a
  fake `Lockable` test double.
* `Mutex`/`ConditionVariable` — **cannot be compiled or run in this
  sandbox** (no Windows SDK, no working MinGW cross-compiler; see
  `File.md`'s identical constraint). Verified by careful manual review
  plus a hand-written mock `<windows.h>` compile+link pass under the
  full `-std=c++17 -fno-exceptions -Wall -Wextra -Wpedantic -Werror` bar
  on both compilers. This must be called out explicitly wherever these
  are described as "done" — implemented, not confirmed, until a real
  `mach build` (or a standalone Visual Studio smoke test, matching
  `ThreadingSmokeTest.cpp`) runs on the actual machine.

---

# Implementation Status

* Architecture: Approved
* Public API: Frozen
* Header/Implementation: Complete (`Mutex.h/.cpp`, `ConditionVariable.h/.cpp`,
  `LockGuard.h`)
* Tests: `LockGuardTest.cpp` — sandbox-verified, all scenarios passing.
  `Mutex`/`ConditionVariable` real mutual-exclusion/wakeup behavior is
  exercised by `ThreadingSmokeTest.cpp` (real-machine only — see
  `Thread.md`).
