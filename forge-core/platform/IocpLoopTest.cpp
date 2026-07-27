// Logic test for IocpLoop, run against the stateful mock IOCP in
// /tmp/win_mock_stateful/windows.h (see that file's header comment).
//
// This verifies IocpLoop.cpp's own control flow — the three-way
// GetQueuedCompletionStatus outcome handling, timer/completion
// interleaving, Run()/RequestStop() — using a fake but behaviorally
// faithful single-threaded completion port. It does NOT verify anything
// about the real Win32 API surface or actual async I/O; that still
// requires building and running this on Windows. Not part of the
// production moz.build build — a throwaway verification harness only,
// same role as TimerSchedulerTest.cpp and the earlier VectorSmokeTest.cpp.

#include "IocpLoop.h"

#include <cstdio>

using namespace forge::core;
using namespace forge::core::platform;

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

struct CompletionRecord
{
    int callCount{ 0 };
    DWORD lastBytesTransferred{ 0 };
    ULONG_PTR lastCompletionKey{ 0 };
    bool lastSucceeded{ false };
    DWORD lastErrorCode{ 0 };
};

void RecordingCallback(IoCompletion* self, DWORD bytesTransferred, ULONG_PTR completionKey, bool succeeded, DWORD errorCode) noexcept
{
    auto* record = static_cast<CompletionRecord*>(self->Pointer);
    ++record->callCount;
    record->lastBytesTransferred = bytesTransferred;
    record->lastCompletionKey = completionKey;
    record->lastSucceeded = succeeded;
    record->lastErrorCode = errorCode;
}

void Test_InitializeSucceeds()
{
    std::printf("Test_InitializeSucceeds...\n");
    mock_iocp::Reset();

    IocpLoop loop;
    Result<void> result = loop.Initialize();
    CHECK(!result.HasError());
}

void Test_PostedCompletionDispatchesOnRunOnce()
{
    std::printf("Test_PostedCompletionDispatchesOnRunOnce...\n");
    mock_iocp::Reset();

    IocpLoop loop;
    CHECK(!loop.Initialize().HasError());

    CompletionRecord record;
    IoCompletion completion{};
    completion.Pointer = &record;
    completion.callback = &RecordingCallback;

    CHECK(!loop.PostCompletion(&completion, 42, 7).HasError());

    CHECK(!loop.RunOnce().HasError());

    CHECK(record.callCount == 1);
    CHECK(record.lastBytesTransferred == 42);
    CHECK(record.lastCompletionKey == 7);
    CHECK(record.lastSucceeded == true);
    CHECK(record.lastErrorCode == 0);

    // Nothing else queued — the next RunOnce should be a plain timeout,
    // not an error, and should not re-dispatch the same completion.
    CHECK(!loop.RunOnce().HasError());
    CHECK(record.callCount == 1);
}

void Test_FailedCompletionReportsFailureNotError()
{
    std::printf("Test_FailedCompletionReportsFailureNotError...\n");
    mock_iocp::Reset();

    IocpLoop loop;
    CHECK(!loop.Initialize().HasError());

    CompletionRecord record;
    IoCompletion completion{};
    completion.Pointer = &record;
    completion.callback = &RecordingCallback;

    // Push directly into the mock queue (bypassing PostCompletion, which
    // always posts a "successful" packet) to simulate a real failed I/O
    // operation that still completed with an overlapped, per
    // GetQueuedCompletionStatus's documented third outcome.
    mock_iocp::Queue().push_back({ 5, 99, &completion, /*simulateFailure=*/true, /*failureCode=*/1234 });

    // RunOnce itself must still succeed — a failed *operation* is reported
    // to that operation's own callback, it is not a failure of the loop.
    CHECK(!loop.RunOnce().HasError());

    CHECK(record.callCount == 1);
    CHECK(record.lastSucceeded == false);
    CHECK(record.lastErrorCode == 1234);
}

void Test_TimerFiresWithNoCompletionPending()
{
    std::printf("Test_TimerFiresWithNoCompletionPending...\n");
    mock_iocp::Reset();

    IocpLoop loop;
    CHECK(!loop.Initialize().HasError());

    int fireCount = 0;
    auto timerCallback = [](void* userData) noexcept { ++(*static_cast<int*>(userData)); };

    // delayMs = 0: due immediately, so the very next RunOnce (which will
    // see an empty queue -> mock "timeout" -> not an error) should still
    // fire it via the post-dispatch PopDue() sweep.
    Result<TimerId> id = loop.ScheduleTimer(0, /*repeat=*/false, timerCallback, &fireCount);
    CHECK(id.HasValue());

    CHECK(!loop.RunOnce().HasError());
    CHECK(fireCount == 1);

    // One-shot: must not fire again.
    CHECK(!loop.RunOnce().HasError());
    CHECK(fireCount == 1);
}

void Test_TimerAndCompletionInSameSweep()
{
    std::printf("Test_TimerAndCompletionInSameSweep...\n");
    mock_iocp::Reset();

    IocpLoop loop;
    CHECK(!loop.Initialize().HasError());

    int fireCount = 0;
    auto timerCallback = [](void* userData) noexcept { ++(*static_cast<int*>(userData)); };
    Result<TimerId> id = loop.ScheduleTimer(0, false, timerCallback, &fireCount);
    CHECK(id.HasValue());

    CompletionRecord record;
    IoCompletion completion{};
    completion.Pointer = &record;
    completion.callback = &RecordingCallback;
    CHECK(!loop.PostCompletion(&completion, 1, 1).HasError());

    // A single RunOnce should dispatch the queued completion AND fire the
    // already-due timer in its trailing PopDue() sweep.
    CHECK(!loop.RunOnce().HasError());
    CHECK(record.callCount == 1);
    CHECK(fireCount == 1);
}

void Test_EmptyAndCountTrackPendingTimers()
{
    std::printf("Test_EmptyAndCountTrackPendingTimers...\n");
    mock_iocp::Reset();

    IocpLoop loop;
    CHECK(!loop.Initialize().HasError());

    CHECK(loop.Empty());
    CHECK(loop.Count() == 0);

    auto noop = [](void*) noexcept {};
    Result<TimerId> a = loop.ScheduleTimer(1000, false, noop, nullptr);
    Result<TimerId> b = loop.ScheduleTimer(2000, false, noop, nullptr);
    CHECK(a.HasValue() && b.HasValue());

    CHECK(!loop.Empty());
    CHECK(loop.Count() == 2);

    loop.CancelTimer(a.Value());
    CHECK(loop.Count() == 1);

    loop.CancelTimer(b.Value());
    CHECK(loop.Empty());
    CHECK(loop.Count() == 0);
}

void Test_GenuineWaitFailureSurfacesAsError()
{
    std::printf("Test_GenuineWaitFailureSurfacesAsError...\n");
    mock_iocp::Reset();

    IocpLoop loop;
    CHECK(!loop.Initialize().HasError());

    mock_iocp::ForceWaitError() = true;
    mock_iocp::ForcedWaitErrorCode() = 999;

    Result<void> result = loop.RunOnce();
    CHECK(result.HasError());
    CHECK(result.Error().Code() == ErrorCode::IOError);
    CHECK(result.Error().NativeCode() == 999);
}

void Test_RequestStopEndsRunAfterCurrentIteration()
{
    std::printf("Test_RequestStopEndsRunAfterCurrentIteration...\n");
    mock_iocp::Reset();

    IocpLoop loop;
    CHECK(!loop.Initialize().HasError());

    struct StopState
    {
        IocpLoop* loop;
        int callCount{ 0 };
    };

    StopState state{ &loop };

    IoCompletion completion{};
    completion.Pointer = &state;
    completion.callback = [](IoCompletion* self, DWORD, ULONG_PTR, bool, DWORD) noexcept
    {
        auto* s = static_cast<StopState*>(self->Pointer);
        ++s->callCount;
        s->loop->RequestStop();
    };

    CHECK(!loop.PostCompletion(&completion, 0, 0).HasError());

    Result<void> result = loop.Run();
    CHECK(!result.HasError());
    CHECK(state.callCount == 1); // Run() stopped after the iteration that called RequestStop
}

} // namespace

int main()
{
    Test_InitializeSucceeds();
    Test_PostedCompletionDispatchesOnRunOnce();
    Test_FailedCompletionReportsFailureNotError();
    Test_TimerFiresWithNoCompletionPending();
    Test_TimerAndCompletionInSameSweep();
    Test_EmptyAndCountTrackPendingTimers();
    Test_GenuineWaitFailureSurfacesAsError();
    Test_RequestStopEndsRunAfterCurrentIteration();

    if (g_failures == 0)
    {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }

    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
