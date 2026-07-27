#pragma once

// Windows-only. This header is only ever reached through IoLoop.h under
// `#if defined(_WIN32)`, so including <windows.h> directly here (rather than
// behind another guard) is intentional — this file has no non-Windows build
// target.
//
// NOT VERIFIED BY COMPILATION. Unlike TimerScheduler (pure logic, fully
// tested under g++/clang++ + ASan/UBSan in a Linux sandbox — see
// HISTORY.md), the IOCP calls here cannot be compiled or run outside a real
// Windows toolchain. This needs to be built and exercised on the project
// owner's machine before it's trusted, the same way the earlier
// VectorSmokeTest.cpp MSVC error was only found once actually built there.

#include <windows.h>

#include "../Types.h"
#include "../Result.h"
#include "TimerScheduler.h"

namespace forge::core::platform
{

/// A single pending asynchronous I/O operation.
///
/// Future socket/file code (Phase 6/5) embeds this as the *first* member of
/// its own per-operation struct (so a `LPOVERLAPPED` handed back by
/// `GetQueuedCompletionStatus` can be reinterpret_cast back to the
/// containing `IoCompletion`, and from there — once that owning struct also
/// puts itself first — back to the request-specific struct). This is the
/// standard zero-virtual-call IOCP idiom: the callback pointer lives inline
/// right next to the OVERLAPPED, so completing an operation costs one
/// function-pointer call, not a vtable lookup.
///
/// `IocpLoop` itself never allocates or owns one of these — the caller
/// (whatever issued the async operation) owns its lifetime and must keep it
/// alive until the completion callback runs.
struct IoCompletion : OVERLAPPED
{
    using CompletionCallback = void (*)(
        IoCompletion* self,
        DWORD bytesTransferred,
        ULONG_PTR completionKey,
        bool succeeded,
        DWORD errorCode);

    CompletionCallback callback{ nullptr };
};

/// Windows IOCP backend for forge::core::platform::IoLoop.
///
/// Single-threaded by design (matches the single-threaded JS event-loop
/// model every other engine — V8/Node, JavaScriptCore/Bun — also uses):
/// exactly one thread calls Run()/RunOnce(). Blocking, CPU-heavy work
/// belongs on the Phase 7 thread pool, not on this loop.
class IocpLoop
{
public:

    IocpLoop() = default;

    IocpLoop(const IocpLoop&) = delete;
    IocpLoop& operator=(const IocpLoop&) = delete;

    ~IocpLoop() noexcept;

    /// Creates the underlying completion port. Must be called exactly once
    /// before any other method.
    [[nodiscard]]
    Result<void> Initialize() noexcept;

    /// Associates a handle (socket, pipe, file — anything IOCP-capable)
    /// with this loop's completion port. `completionKey` is returned
    /// verbatim in every completion for operations on that handle, so
    /// callers can use it to identify which object a completion belongs to
    /// without a lookup table, if they want to.
    [[nodiscard]]
    Result<void> AssociateHandle(
        HANDLE handle,
        ULONG_PTR completionKey) noexcept;

    /// Registers a timer. See TimerScheduler::Schedule for semantics —
    /// this just supplies the clock (steady_clock-based, milliseconds
    /// since an arbitrary but process-lifetime-stable epoch).
    [[nodiscard]]
    Result<TimerId> ScheduleTimer(
        u64 delayMs,
        bool repeat,
        TimerCallback callback,
        void* userData) noexcept;

    void CancelTimer(TimerId id) noexcept;

    /// True if there are no pending (non-cancelled) timers. A caller
    /// driving the loop from the outside (see the RunUntilIdle pattern in
    /// forge.cpp) uses this — together with its own notion of other
    /// pending work, like a JS job queue — to decide when it's safe to
    /// stop calling RunOnce()/Run(). Note this does not yet account for
    /// pending async I/O operations posted via AssociateHandle — Phase 5/6
    /// will need to add an active-operation count for that, the same way
    /// libuv/Node track "active handles".
    [[nodiscard]]
    bool Empty() const noexcept;

    [[nodiscard]]
    Size Count() const noexcept;

    /// Queues a completion manually, without a real I/O operation —
    /// e.g. to wake Run() from another thread, or to defer work to the
    /// next loop iteration. `completion` must stay alive until its
    /// callback runs, same as for a real I/O completion.
    [[nodiscard]]
    Result<void> PostCompletion(
        IoCompletion* completion,
        DWORD bytesTransferred,
        ULONG_PTR completionKey) noexcept;

    /// Runs one iteration: waits for either the next I/O completion or the
    /// next due timer (whichever comes first), dispatches whatever fired,
    /// then fires any timers now due. Returns success even if nothing was
    /// ready and the wait simply timed out — that's the expected way an
    /// iteration ends when only timers, no I/O, are pending.
    [[nodiscard]]
    Result<void> RunOnce() noexcept;

    /// Calls RunOnce() until RequestStop() is called. Intended as the
    /// process's main loop once the runtime is wired up (Phase 8); until
    /// then RunOnce() is the more useful entry point for testing pieces in
    /// isolation.
    [[nodiscard]]
    Result<void> Run() noexcept;

    /// Safe to call from within a completion or timer callback running on
    /// the loop's own thread. Not safe to call from another thread without
    /// additional synchronization — cross-thread wakeup is what
    /// PostCompletion() is for.
    void RequestStop() noexcept;

private:

    [[nodiscard]]
    static u64 NowMs() noexcept;

    HANDLE port_{ nullptr };
    TimerScheduler timers_;
    bool running_{ false };
};

} // namespace forge::core::platform
