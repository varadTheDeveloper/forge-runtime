// Implementation of ThreadPool — see Thread.md. Built entirely on top of
// Mutex/ConditionVariable/Thread's own abstractions, so this file itself
// has no direct <windows.h> dependency — but every one of those pieces
// is Win32-only (see their own .cpp files), so the whole pool is
// transitively Win32-only too. The #error guard below exists purely so a
// reader of this file alone (without tracing through Mutex.cpp/
// ConditionVariable.cpp/Thread.cpp) immediately sees that constraint,
// matching every other OS-facing component in forge-core.
//
// Cannot be exercised in the sandbox this was written in — verified by
// manual review plus a hand-written mock <windows.h> compile+link pass
// (transitively, via Mutex/ConditionVariable/Thread) only. Needs a real
// `mach build` (or a standalone Visual Studio smoke test) to go from
// "implemented" to "confirmed working" — flagged explicitly per
// AGENTS.md's "Be Honest".

#if !defined(_WIN32)
#error "forge::core::ThreadPool: no backend implemented for this platform (Win32 only, matching File/IoLoop's own precedent)."
#endif

#include "ThreadPool.h"

#include "LockGuard.h"

namespace forge::core
{

ThreadPool::ThreadPool() noexcept
    :
    allocator_(nullptr)
{
}

ThreadPool::~ThreadPool() noexcept
{
    Shutdown();
}

Result<void> ThreadPool::Initialize(
    memory::Allocator& allocator,
    Size threadCount)
{
    if (allocator_ != nullptr)
    {
        return Result<void>(Failure{ Error(ErrorCode::InvalidOperation) }); // already initialized
    }

    if (threadCount == 0)
    {
        return Result<void>(Failure{ Error(ErrorCode::InvalidArgument) });
    }

    allocator_ = &allocator;
    stopping_ = false;
    tasks_ = Queue<Task>(allocator);
    workers_ = Vector<Thread>(allocator);

    if (Result<void> reserved = workers_.Reserve(threadCount); reserved.HasError())
    {
        allocator_ = nullptr;
        return reserved;
    }

    for (Size i = 0; i < threadCount; ++i)
    {
        Result<Thread> worker = Thread::Create(allocator, [this]() noexcept { WorkerLoop(); });

        if (worker.HasError())
        {
            // Roll back whatever we already spawned rather than leaving
            // live threads running against a pool we're about to report
            // as failed to initialize — a caller that sees Initialize()
            // fail should be able to assume nothing was left behind.
            const Error failure = worker.Error();
            Shutdown();
            return Result<void>(Failure{ failure });
        }

        if (Result<void> pushed = workers_.PushBack(std::move(worker.Value())); pushed.HasError())
        {
            // Reserve() above already secured capacity for `threadCount`
            // entries, so this should be unreachable in practice — but
            // Vector<T>::PushBack still returns Result<void>, and it
            // would be dishonest to assume it can't fail here just
            // because it's not expected to.
            //
            // The thread we just failed to record is already running
            // WorkerLoop() and blocked in Wait() (queue is empty,
            // stopping_ is still false at this point) — joining it
            // directly here would deadlock, since nothing has told it to
            // stop yet. Shutdown() first (sets stopping_ under mutex_
            // and calls NotifyAll(), which this orphaned thread receives
            // exactly like any tracked one — the stopping_ check inside
            // WorkerLoop happens under the same mutex_, so there is no
            // missed-wakeup race here even though this thread was never
            // pushed into workers_) makes it safe to then join directly.
            Thread& orphaned = worker.Value();
            Shutdown();
            orphaned.Join().Ignore();
            return pushed;
        }
    }

    return {};
}

Result<void> ThreadPool::Initialize(
    Size threadCount)
{
    return Initialize(memory::GetDefaultAllocator(), threadCount);
}

Result<void> ThreadPool::SubmitErased(
    detail::ErasedCallable erased)
{
    LockGuard<Mutex> guard(mutex_);

    if (stopping_ || allocator_ == nullptr)
    {
        return Result<void>(Failure{ Error(ErrorCode::InvalidOperation) });
    }

    Task task{ erased.closure, erased.invoke };

    if (Result<void> pushed = tasks_.Push(task); pushed.HasError())
    {
        return pushed;
    }

    // Calling NotifyOne() while still holding mutex_ (rather than after
    // the LockGuard releases it) is simpler to reason about and is fully
    // supported by SleepConditionVariableSRW/WakeConditionVariable —
    // Win32 does not require the notifier to have released the lock
    // first, unlike some other condition-variable APIs' documented best
    // practice of unlocking first purely as a scheduling optimization.
    wakeWorker_.NotifyOne();

    return {};
}

void ThreadPool::WorkerLoop() noexcept
{
    for (;;)
    {
        Task task{};

        {
            LockGuard<Mutex> guard(mutex_);

            while (tasks_.Empty() && !stopping_)
            {
                // A Wait() failure here is a genuinely exceptional OS
                // error (see ConditionVariable.h) — there is nothing
                // more useful to do with it on a background worker
                // thread than retry the predicate check, which the
                // enclosing while loop already does.
                wakeWorker_.Wait(mutex_).Ignore();
            }

            if (tasks_.Empty())
            {
                // stopping_ is true and nothing is left queued: drain
                // complete, this worker can exit.
                return;
            }

            task = tasks_.Front();
            tasks_.Pop();
        }

        task.invoke(task.closure);
    }
}

void ThreadPool::Shutdown() noexcept
{
    {
        LockGuard<Mutex> guard(mutex_);
        stopping_ = true;
    }

    // Every worker is currently either blocked in Wait() (about to wake,
    // re-check stopping_, and exit once the queue is empty) or busy
    // running a task (which will loop back around, re-check, and exit
    // the same way) — NotifyAll() wakes every waiter so none of them is
    // left blocked indefinitely.
    wakeWorker_.NotifyAll();

    for (Size i = 0; i < workers_.Size(); ++i)
    {
        if (workers_[i].Joinable())
        {
            workers_[i].Join().Ignore();
        }
    }

    workers_.Clear();
    allocator_ = nullptr;
}

Size ThreadPool::ThreadCount() const noexcept
{
    return workers_.Size();
}

bool ThreadPool::Initialized() const noexcept
{
    return allocator_ != nullptr;
}

} // namespace forge::core
