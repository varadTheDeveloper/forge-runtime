#include "IocpLoop.h"

#include <chrono>

#include "../Assert.h"

// NOT VERIFIED BY COMPILATION — see the note at the top of IocpLoop.h.

namespace forge::core::platform
{

namespace
{

// INFINITE (0xFFFFFFFF) is reserved to mean "no timers pending, wait
// forever" — clamp any real delay below it so a legitimately huge delay
// value can never be misread as "wait forever".
constexpr u64 kMaxTimeoutMs = 0xFFFFFFFEu;

} // namespace

IocpLoop::~IocpLoop() noexcept
{
    if (port_ != nullptr)
    {
        CloseHandle(port_);
        port_ = nullptr;
    }
}

u64 IocpLoop::NowMs() noexcept
{
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

Result<void> IocpLoop::Initialize() noexcept
{
    FORGE_ASSERT(port_ == nullptr); // Initialize() must only be called once.

    // NumberOfConcurrentThreads = 1: exactly one thread ever calls
    // Run()/RunOnce() (see the class comment) — no reason to let the OS
    // wake more than one.
    port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);

    if (port_ == nullptr)
    {
        return Result<void>(Failure(Error(ErrorCode::IOError, static_cast<i32>(GetLastError()))));
    }

    return Result<void>();
}

Result<void> IocpLoop::AssociateHandle(HANDLE handle, ULONG_PTR completionKey) noexcept
{
    FORGE_ASSERT(port_ != nullptr); // Initialize() must run first.

    HANDLE result = CreateIoCompletionPort(handle, port_, completionKey, 0);

    if (result == nullptr)
    {
        return Result<void>(Failure(Error(ErrorCode::IOError, static_cast<i32>(GetLastError()))));
    }

    return Result<void>();
}

Result<TimerId> IocpLoop::ScheduleTimer(
    u64 delayMs,
    bool repeat,
    TimerCallback callback,
    void* userData) noexcept
{
    return timers_.Schedule(NowMs(), delayMs, repeat, callback, userData);
}

void IocpLoop::CancelTimer(TimerId id) noexcept
{
    timers_.Cancel(id);
}

bool IocpLoop::Empty() const noexcept
{
    return timers_.Empty();
}

Size IocpLoop::Count() const noexcept
{
    return timers_.Count();
}

Result<void> IocpLoop::PostCompletion(
    IoCompletion* completion,
    DWORD bytesTransferred,
    ULONG_PTR completionKey) noexcept
{
    FORGE_ASSERT(port_ != nullptr);
    FORGE_ASSERT(completion != nullptr);

    BOOL ok = PostQueuedCompletionStatus(
        port_, bytesTransferred, completionKey, static_cast<LPOVERLAPPED>(completion));

    if (!ok)
    {
        return Result<void>(Failure(Error(ErrorCode::IOError, static_cast<i32>(GetLastError()))));
    }

    return Result<void>();
}

Result<void> IocpLoop::RunOnce() noexcept
{
    FORGE_ASSERT(port_ != nullptr); // Initialize() must run first.

    u64 now = NowMs();
    u64 delayMs = 0;
    DWORD timeoutMs = INFINITE;

    if (timers_.NextDueDelay(now, delayMs))
    {
        timeoutMs = static_cast<DWORD>(delayMs > kMaxTimeoutMs ? kMaxTimeoutMs : delayMs);
    }

    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED overlapped = nullptr;

    BOOL ok = GetQueuedCompletionStatus(port_, &bytesTransferred, &completionKey, &overlapped, timeoutMs);

    if (!ok && overlapped == nullptr)
    {
        // The wait itself did not produce a completion. A plain timeout
        // (we asked to wait `timeoutMs` and nothing showed up) is the
        // expected, non-error way an iteration ends when only timers are
        // pending — anything else is a genuine failure in the wait call.
        DWORD err = GetLastError();
        if (err != WAIT_TIMEOUT)
        {
            return Result<void>(Failure(Error(ErrorCode::IOError, static_cast<i32>(err))));
        }
    }
    else if (overlapped != nullptr)
    {
        // A completion packet was dequeued, whether or not the underlying
        // I/O operation itself succeeded (per GetQueuedCompletionStatus's
        // documented three-way return: success, timeout/wait-failure with
        // no overlapped, or a failed operation that still completed with
        // an overlapped). Either way it's this specific operation's
        // outcome to report, not RunOnce()'s.
        DWORD errorCode = ok ? 0 : GetLastError();
        auto* completion = static_cast<IoCompletion*>(overlapped);

        FORGE_ASSERT(completion->callback != nullptr);
        if (completion->callback != nullptr)
        {
            completion->callback(completion, bytesTransferred, completionKey, ok != FALSE, errorCode);
        }
    }

    // Fire whatever timers are due now, whether or not an I/O completion
    // also fired this iteration — both can legitimately be ready at once.
    timers_.PopDue(NowMs());

    return Result<void>();
}

Result<void> IocpLoop::Run() noexcept
{
    running_ = true;

    while (running_)
    {
        Result<void> result = RunOnce();
        if (result.HasError())
        {
            running_ = false;
            return result;
        }
    }

    return Result<void>();
}

void IocpLoop::RequestStop() noexcept
{
    running_ = false;
}

} // namespace forge::core::platform
