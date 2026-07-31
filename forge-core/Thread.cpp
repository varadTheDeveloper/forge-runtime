// Win32 implementation of Thread — see Thread.md. Cannot be compiled in
// the sandbox this was written in (no Windows SDK / working MinGW cross
// compiler available there); verified by manual review plus a
// hand-written mock <windows.h> compile+link pass only. Needs a real
// `mach build` to go from "implemented" to "confirmed working" — flagged
// explicitly per AGENTS.md's "Be Honest".

#if !defined(_WIN32)
#error "forge::core::Thread: no backend implemented for this platform (Win32 only, matching File/IoLoop's own precedent)."
#endif

#include "Thread.h"

#include "Assert.h"
#include "Construct.h"

#include <windows.h>

#include "platform/Win32Error.h"

namespace forge::core
{

namespace
{

/// Glue struct that gets `this`-erased-thread's ErasedCallable across
/// the CreateThread boundary, which only accepts a fixed
/// `DWORD WINAPI (*)(LPVOID)` signature — not the erasure's own
/// `void(*)(void*)` shape. Allocated through the same Allocator the
/// caller passed to Thread::Create, per the project rule that every
/// allocation goes through memory::Allocator (never raw new/delete).
struct ThreadStart
{
    detail::ErasedCallable erased;
    memory::Allocator* allocator;
};

DWORD WINAPI Win32ThreadProc(
    LPVOID param) noexcept
{
    ThreadStart* start = static_cast<ThreadStart*>(param);
    const detail::ErasedCallable erased = start->erased;
    memory::Allocator* allocator = start->allocator;

    start->~ThreadStart();
    allocator->Deallocate(start, sizeof(ThreadStart), alignof(ThreadStart));

    // Runs the user's callable and, as part of the same call, destroys
    // its closure — see ErasedCallable's own comment.
    erased.invoke(erased.closure);

    return 0;
}

} // namespace

Thread::Thread() noexcept
    :
    handle_(nullptr)
{
}

Thread::Thread(
    void* handle) noexcept
    :
    handle_(handle)
{
}

Thread::Thread(
    Thread&& other) noexcept
    :
    handle_(other.handle_)
{
    other.handle_ = nullptr;
}

Thread& Thread::operator=(
    Thread&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    FORGE_ASSERT(!Joinable()); // must Join()/Detach() before being overwritten

    handle_ = other.handle_;
    other.handle_ = nullptr;

    return *this;
}

Thread::~Thread() noexcept
{
    FORGE_ASSERT(!Joinable()); // must Join()/Detach() before destruction
}

Result<Thread> Thread::CreateWithErasedCallable(
    memory::Allocator& allocator,
    detail::ErasedCallable closure)
{
    void* raw = allocator.Allocate(sizeof(ThreadStart), alignof(ThreadStart));

    if (raw == nullptr)
    {
        return Result<Thread>(Failure{ Error(ErrorCode::OutOfMemory) });
    }

    ThreadStart* start = detail::ConstructAt<ThreadStart>(
        static_cast<ThreadStart*>(raw),
        ThreadStart{ closure, &allocator });

    HANDLE handle = CreateThread(
        nullptr,           // default security attributes
        0,                 // default stack size
        &Win32ThreadProc,
        start,
        0,                 // run immediately (not CREATE_SUSPENDED)
        nullptr);          // don't need the OS thread id

    if (handle == nullptr)
    {
        const DWORD lastError = GetLastError();

        start->~ThreadStart();
        allocator.Deallocate(raw, sizeof(ThreadStart), alignof(ThreadStart));

        return Result<Thread>(Failure{ platform::TranslateWin32Error(lastError) });
    }

    return Result<Thread>(Thread(handle));
}

bool Thread::Joinable() const noexcept
{
    return handle_ != nullptr;
}

Result<void> Thread::Join() noexcept
{
    if (!Joinable())
    {
        return Result<void>(Failure{ Error(ErrorCode::InvalidOperation) });
    }

    const DWORD waitResult = WaitForSingleObject(static_cast<HANDLE>(handle_), INFINITE);

    if (waitResult != WAIT_OBJECT_0)
    {
        return Result<void>(Failure{ platform::TranslateWin32Error(GetLastError()) });
    }

    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;

    return {};
}

void Thread::Detach() noexcept
{
    if (Joinable())
    {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

} // namespace forge::core
