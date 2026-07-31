// Standalone correctness test for LockGuard — pure RAII logic with zero
// OS dependency, so unlike Mutex itself, this gets full sandbox
// verification against a fake Lockable test double rather than a real
// Win32 SRWLOCK. Compiled under C++17/exceptions-disabled from the
// start, per the user's standing instruction to always target the real
// mach build's constraints.

#include "LockGuard.h"

#include <cstdio>

using namespace forge::core;

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                          \
    do                                                                       \
    {                                                                        \
        if (!(cond))                                                        \
        {                                                                    \
            std::fprintf(stderr, "CHECK FAILED at %s:%d: %s\n", __FILE__,    \
                          __LINE__, #cond);                                  \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

/// Fake Lockable — records Lock()/Unlock() calls and how many times each
/// happened, and asserts (via CHECK, not FORGE_ASSERT, so a bug here is a
/// test failure rather than a program abort) that Lock/Unlock alternate
/// correctly rather than double-locking or double-unlocking.
class FakeLockable
{
public:
    void Lock() noexcept
    {
        CHECK(!locked_);
        locked_ = true;
        ++lockCount_;
    }

    void Unlock() noexcept
    {
        CHECK(locked_);
        locked_ = false;
        ++unlockCount_;
    }

    [[nodiscard]] bool IsLocked() const noexcept { return locked_; }
    [[nodiscard]] int LockCount() const noexcept { return lockCount_; }
    [[nodiscard]] int UnlockCount() const noexcept { return unlockCount_; }

private:
    bool locked_{ false };
    int lockCount_{ 0 };
    int unlockCount_{ 0 };
};

void Test_LockGuard_LocksOnConstructionUnlocksOnDestruction()
{
    std::printf("Test_LockGuard_LocksOnConstructionUnlocksOnDestruction...\n");

    FakeLockable lockable;
    CHECK(!lockable.IsLocked());

    {
        LockGuard<FakeLockable> guard(lockable);
        CHECK(lockable.IsLocked());
        CHECK(lockable.LockCount() == 1);
        CHECK(lockable.UnlockCount() == 0);
    }

    CHECK(!lockable.IsLocked());
    CHECK(lockable.LockCount() == 1);
    CHECK(lockable.UnlockCount() == 1);
}

void Test_LockGuard_SequentialScopesLockAndUnlockEachTime()
{
    std::printf("Test_LockGuard_SequentialScopesLockAndUnlockEachTime...\n");

    FakeLockable lockable;

    for (int i = 0; i < 5; ++i)
    {
        LockGuard<FakeLockable> guard(lockable);
        CHECK(lockable.IsLocked());
    }

    CHECK(!lockable.IsLocked());
    CHECK(lockable.LockCount() == 5);
    CHECK(lockable.UnlockCount() == 5);
}

void Test_LockGuard_UnlocksOnEarlyReturnPath()
{
    std::printf("Test_LockGuard_UnlocksOnEarlyReturnPath...\n");

    FakeLockable lockable;

    auto guardedWork = [&lockable](bool takeEarlyPath) {
        LockGuard<FakeLockable> guard(lockable);

        if (takeEarlyPath)
        {
            return; // guard's destructor must still run here
        }
    };

    guardedWork(true);
    CHECK(!lockable.IsLocked());
    CHECK(lockable.LockCount() == 1);
    CHECK(lockable.UnlockCount() == 1);

    guardedWork(false);
    CHECK(!lockable.IsLocked());
    CHECK(lockable.LockCount() == 2);
    CHECK(lockable.UnlockCount() == 2);
}

} // namespace

int main()
{
    Test_LockGuard_LocksOnConstructionUnlocksOnDestruction();
    Test_LockGuard_SequentialScopesLockAndUnlockEachTime();
    Test_LockGuard_UnlocksOnEarlyReturnPath();

    if (g_failures == 0)
    {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }

    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
