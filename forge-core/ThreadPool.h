#pragma once

#include <utility>

#include "ConditionVariable.h"
#include "ErasedCallable.h"
#include "Error.h"
#include "Mutex.h"
#include "Queue.h"
#include "Result.h"
#include "Thread.h"
#include "Types.h"
#include "memory/Allocator.h"
#include "memory/DefaultAllocator.h"
#include "memory/ResultVoid.h"
#include "memory/Vector.h"

namespace forge::core
{

/// A fixed-size pool of worker threads pulling tasks off a shared queue.
/// See Thread.md for the full spec.
///
/// Follows IocpLoop's own "default-construct, then Initialize()" shape
/// rather than a `static Create()` factory returning ThreadPool by
/// value: every worker thread's loop captures a pointer back to this
/// ThreadPool instance, so the pool needs a stable address from the
/// moment the first worker spawns. A `Result<ThreadPool> Create(...)`
/// factory would require ThreadPool to be movable (to relocate the
/// returned value out of the factory) — but a moved ThreadPool would
/// leave every already-spawned worker's captured pointer referencing the
/// old, abandoned location. Non-movable + Initialize() sidesteps this
/// entirely, the same way IocpLoop::Initialize() already does for the
/// same underlying reason (a completion port and its callbacks are also
/// tied to a stable address).
class [[nodiscard]] ThreadPool
{
public:

    ThreadPool() noexcept;

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    ~ThreadPool() noexcept;

    /// Spawns `threadCount` worker threads (must be >= 1). Must be
    /// called at most once. Every allocation the pool itself needs — the
    /// task queue's storage, the worker Vector<Thread>, and every
    /// Submit()'d task's closure — comes from `allocator`, which must
    /// outlive the pool. On failure, no threads are left running (any
    /// partially-spawned workers are stopped and joined before
    /// returning).
    [[nodiscard]]
    Result<void> Initialize(
        memory::Allocator& allocator,
        Size threadCount);

    [[nodiscard]]
    Result<void> Initialize(
        Size threadCount);

    /// Enqueues `callable` to run on some worker thread once one becomes
    /// free. Fails only on OOM (allocating the task's closure, or
    /// growing the internal queue) or if the pool isn't initialized, or
    /// has already had Shutdown() called.
    template<typename Callable>
    Result<void> Submit(
        Callable callable);

    /// Signals every worker to stop once it has drained whatever is
    /// already queued (a task submitted before Shutdown() is called
    /// still runs; nothing new can be Submit()'d afterwards), then joins
    /// them all. Safe to call more than once, and safe to call on a
    /// never-Initialize()'d pool (a no-op in that case). The destructor
    /// calls this automatically if it wasn't already called explicitly.
    void Shutdown() noexcept;

    [[nodiscard]]
    Size ThreadCount() const noexcept;

    [[nodiscard]]
    bool Initialized() const noexcept;

private:

    struct Task
    {
        void* closure;
        void (*invoke)(void*) noexcept;
    };

    Result<void> SubmitErased(
        detail::ErasedCallable erased);

    void WorkerLoop() noexcept;

    memory::Allocator* allocator_{ nullptr };
    Vector<Thread> workers_;
    Queue<Task> tasks_;
    Mutex mutex_;
    ConditionVariable wakeWorker_;
    bool stopping_{ false };
};

} // namespace forge::core

#include "ThreadPool.inl"
