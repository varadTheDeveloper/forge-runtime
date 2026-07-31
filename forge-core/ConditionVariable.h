#pragma once

#include "Mutex.h"
#include "Result.h"
#include "Types.h"
#include "memory/ResultVoid.h"

namespace forge::core
{

/// Win32 CONDITION_VARIABLE wrapper, always used together with a Mutex —
/// see Sync.md for the full spec.
///
/// Usage mirrors std::condition_variable + std::unique_lock, except
/// forge-core's Mutex has no separate lock-ownership wrapper type: the
/// caller locks/unlocks the Mutex directly (or via LockGuard<Mutex>) and
/// must hold it locked before calling Wait/WaitFor. Same spurious-wakeup
/// contract as the Win32 primitive and std::condition_variable itself —
/// a woken waiter must re-check its own predicate in a loop, not assume
/// the condition it was waiting for is actually true.
class ConditionVariable
{
public:

    ConditionVariable() noexcept;

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    ConditionVariable(ConditionVariable&&) = delete;
    ConditionVariable& operator=(ConditionVariable&&) = delete;

    ~ConditionVariable() noexcept = default; // CONDITION_VARIABLE needs no explicit destruction

    /// Blocks until notified. `mutex` must already be locked by the
    /// calling thread; it is atomically unlocked while waiting and
    /// re-locked before this returns. Only fails for genuinely
    /// exceptional OS errors — a plain wakeup is success, not a signal
    /// that the awaited condition is actually true (see class comment).
    Result<void> Wait(
        Mutex& mutex) noexcept;

    /// Same contract as Wait, but gives up after `timeoutMs`
    /// milliseconds. Returns true if notified, false if the wait timed
    /// out — a timeout is not itself an Error.
    [[nodiscard]]
    Result<bool> WaitFor(
        Mutex& mutex,
        u32 timeoutMs) noexcept;

    void NotifyOne() noexcept;

    void NotifyAll() noexcept;

private:

    // Opaque storage for a Win32 CONDITION_VARIABLE — same reasoning as
    // Mutex::native_ (documented as exactly one pointer-sized,
    // zero-initializable value; ConditionVariable.cpp static_asserts
    // this).
    void* native_{ nullptr };
};

} // namespace forge::core
