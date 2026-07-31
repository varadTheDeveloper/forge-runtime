// Win32 implementation of ConditionVariable — see Sync.md. Cannot be
// compiled in the sandbox this was written in (no Windows SDK / working
// MinGW cross compiler available there); verified by manual review plus
// a hand-written mock <windows.h> compile+link pass only. Needs a real
// `mach build` to go from "implemented" to "confirmed working" — flagged
// explicitly per AGENTS.md's "Be Honest".

#if !defined(_WIN32)
#error "forge::core::ConditionVariable: no backend implemented for this platform (Win32 only, matching File/IoLoop's own precedent)."
#endif

#include "ConditionVariable.h"

#include <windows.h>

#include "platform/Win32Error.h"

namespace forge::core
{

namespace
{

static_assert(
    sizeof(CONDITION_VARIABLE) == sizeof(void*) && alignof(CONDITION_VARIABLE) == alignof(void*),
    "forge::core::ConditionVariable assumes CONDITION_VARIABLE is exactly "
    "one pointer-sized, zero-initializable value (true since Windows "
    "Vista) so it can store one behind an opaque void* without "
    "#including <windows.h> in ConditionVariable.h. If this fails, "
    "ConditionVariable's storage strategy needs revisiting.");

[[nodiscard]]
PCONDITION_VARIABLE NativeHandle(
    void* storage) noexcept
{
    return reinterpret_cast<PCONDITION_VARIABLE>(storage);
}

} // namespace

ConditionVariable::ConditionVariable() noexcept
    :
    native_(nullptr) // zero-initialized == CONDITION_VARIABLE_INIT
{
}

Result<void> ConditionVariable::Wait(
    Mutex& mutex) noexcept
{
    // mutex.NativePtr() is private to Mutex, accessible here only because
    // Mutex declares `friend class ConditionVariable;` — that friendship
    // extends to this member function, but NOT to a free helper function
    // in the anonymous namespace above, which is why this cast is written
    // out inline in both Wait() and WaitFor() rather than factored into
    // one.
    PSRWLOCK nativeMutex = reinterpret_cast<PSRWLOCK>(mutex.NativePtr());

    if (!SleepConditionVariableSRW(NativeHandle(&native_), nativeMutex, INFINITE, 0))
    {
        return Result<void>(Failure{ platform::TranslateWin32Error(GetLastError()) });
    }

    return {};
}

Result<bool> ConditionVariable::WaitFor(
    Mutex& mutex,
    u32 timeoutMs) noexcept
{
    PSRWLOCK nativeMutex = reinterpret_cast<PSRWLOCK>(mutex.NativePtr());

    if (SleepConditionVariableSRW(NativeHandle(&native_), nativeMutex, timeoutMs, 0))
    {
        return Result<bool>(true);
    }

    const DWORD lastError = GetLastError();

    if (lastError == ERROR_TIMEOUT)
    {
        return Result<bool>(false);
    }

    return Result<bool>(Failure{ platform::TranslateWin32Error(lastError) });
}

void ConditionVariable::NotifyOne() noexcept
{
    WakeConditionVariable(NativeHandle(&native_));
}

void ConditionVariable::NotifyAll() noexcept
{
    WakeAllConditionVariable(NativeHandle(&native_));
}

} // namespace forge::core
