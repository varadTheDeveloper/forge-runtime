#pragma once

namespace forge::core
{

class ConditionVariable;

/// Non-recursive mutual-exclusion lock. Win32-only backend (SRWLOCK) —
/// see Sync.md for the full spec.
///
/// Deliberately not recursive: a thread that already holds the lock and
/// calls Lock() again deadlocks itself, rather than silently succeeding.
/// This matches SRWLOCK's own native behavior and avoids the recursive-
/// mutex antipattern (code that "just" locks again to be safe usually
/// has a design problem worth surfacing, not papering over).
///
/// Never fails to construct or lock — SRWLOCK requires no heap
/// allocation and its initialization cannot fail, unlike a Win32
/// CRITICAL_SECTION (whose InitializeCriticalSection can, in principle,
/// raise an OS exception under extreme low-memory conditions). This is
/// why Mutex has no Create() factory and Lock()/Unlock() return void
/// rather than Result<void>.
class Mutex
{
public:

    Mutex() noexcept;

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    ~Mutex() noexcept = default; // SRWLOCK needs no explicit destruction

    void Lock() noexcept;

    void Unlock() noexcept;

    /// Returns true and acquires the lock if it was free; returns false
    /// immediately (without blocking) if it was already held.
    [[nodiscard]]
    bool TryLock() noexcept;

private:

    friend class ConditionVariable;

    /// For ConditionVariable's use only — Win32's SleepConditionVariableSRW
    /// needs the raw SRWLOCK* this Mutex wraps. Not part of Mutex's public
    /// contract; nothing outside ConditionVariable.cpp should call this.
    [[nodiscard]]
    void* NativePtr() noexcept
    {
        return &native_;
    }

    // Opaque storage for a Win32 SRWLOCK. Microsoft documents SRWLOCK as
    // exactly one pointer-sized value, zero-initializable in place of
    // calling InitializeSRWLock (this is what the SRWLOCK_INIT macro
    // itself expands to) — so a zero-initialized void* here is bit-for-
    // bit equivalent to a properly-initialized SRWLOCK, without needing
    // <windows.h> in this header (only Mutex.cpp includes it — same
    // reasoning as File's HANDLE-as-void*). Mutex.cpp static_asserts
    // this size/alignment assumption so a future Windows SDK that broke
    // it would fail loudly at compile time rather than corrupt memory
    // silently.
    void* native_{ nullptr };
};

} // namespace forge::core
