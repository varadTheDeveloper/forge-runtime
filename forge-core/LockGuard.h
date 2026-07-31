#pragma once

namespace forge::core
{

/// RAII scoped lock, mirroring std::lock_guard's shape but named to match
/// this project's own conventions. Locks `lockable` in the constructor,
/// unlocks it in the destructor — never throws (`Mutex::Lock`/`Unlock`
/// don't fail; see Sync.md), so no exception-safety story is needed here
/// the way MakeUnique's placement-new guard needs one.
///
/// Templated on the lockable type rather than hardcoded to `Mutex` so a
/// test can exercise this logic against a fake, sandbox-only lockable
/// without needing a real Win32 SRWLOCK — see LockGuardTest.cpp. Any
/// type with `Lock()`/`Unlock()` methods works.
///
/// Non-copyable and non-movable: a moved-from LockGuard would still run
/// its destructor and try to unlock a lock it no longer (conceptually)
/// holds, which is exactly the kind of double-unlock bug RAII exists to
/// prevent. std::lock_guard makes the same choice for the same reason.
template<typename Lockable>
class [[nodiscard]] LockGuard
{
public:

    explicit LockGuard(
        Lockable& lockable) noexcept
        :
        lockable_(lockable)
    {
        lockable_.Lock();
    }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

    LockGuard(LockGuard&&) = delete;
    LockGuard& operator=(LockGuard&&) = delete;

    ~LockGuard()
    {
        lockable_.Unlock();
    }

private:

    Lockable& lockable_;
};

} // namespace forge::core
