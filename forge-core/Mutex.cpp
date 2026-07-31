// Win32 implementation of Mutex — see Sync.md. Cannot be compiled in the
// sandbox this was written in (no Windows SDK / working MinGW cross
// compiler available there); verified by manual review plus a
// hand-written mock <windows.h> compile+link pass only. Needs a real
// `mach build` to go from "implemented" to "confirmed working" — flagged
// explicitly per AGENTS.md's "Be Honest", same as File/Path was before
// its own real-build confirmation.

#if !defined(_WIN32)
#error "forge::core::Mutex: no backend implemented for this platform (Win32 only, matching File/IoLoop's own precedent)."
#endif

#include "Mutex.h"

#include <windows.h>

namespace forge::core
{

namespace
{

static_assert(
    sizeof(SRWLOCK) == sizeof(void*) && alignof(SRWLOCK) == alignof(void*),
    "forge::core::Mutex assumes SRWLOCK is exactly one pointer-sized, "
    "zero-initializable value (true since Windows Vista) so it can store "
    "one behind an opaque void* without #including <windows.h> in Mutex.h. "
    "If this fails, Mutex's storage strategy needs revisiting.");

[[nodiscard]]
PSRWLOCK NativeHandle(
    void* storage) noexcept
{
    return reinterpret_cast<PSRWLOCK>(storage);
}

} // namespace

Mutex::Mutex() noexcept
    :
    native_(nullptr) // zero-initialized == SRWLOCK_INIT, no InitializeSRWLock call needed
{
}

void Mutex::Lock() noexcept
{
    AcquireSRWLockExclusive(NativeHandle(&native_));
}

void Mutex::Unlock() noexcept
{
    ReleaseSRWLockExclusive(NativeHandle(&native_));
}

bool Mutex::TryLock() noexcept
{
    return TryAcquireSRWLockExclusive(NativeHandle(&native_)) != 0;
}

} // namespace forge::core
