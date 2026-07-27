// Windows-only smoke test for IocpLoop — build and run this on the real
// machine the way VectorSmokeTest.cpp was used to catch the earlier MSVC
// build error. This is a separate console-app project/target from
// forge.exe (same reasoning as VectorSmokeTest.cpp: both have their own
// main() and are not added to forge/moz.build).
//
// This does NOT exercise real async file/socket I/O yet (Phase 5/6 will
// add that) — it only proves the loop itself builds against the real
// Win32 API and correctly runs timers plus a manually-posted completion,
// which is everything IocpLoop currently does.
//
// Expected output, if everything works:
//   IocpSmokeTest: loop initialized
//   IocpSmokeTest: posted completion fired (bytes=7, key=42, ok=1)
//   IocpSmokeTest: timer fired (count=1)
//   IocpSmokeTest: timer fired (count=2)
//   IocpSmokeTest: timer fired (count=3)
//   IocpSmokeTest: all checks passed

#include "forge-core/platform/IoLoop.h"

#include <cstdio>
#include <cstdlib>

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

struct CompletionState
{
    bool fired{ false };
    DWORD bytesTransferred{ 0 };
    ULONG_PTR completionKey{ 0 };
    bool succeeded{ false };
};

struct TimerState
{
    IoLoop* loop{ nullptr };
    int fireCount{ 0 };
};

} // namespace

int main()
{
    IoLoop loop;

    Result<void> initResult = loop.Initialize();
    CHECK(!initResult.HasError());
    std::printf("IocpSmokeTest: loop initialized\n");

    // 1) A manually-posted completion — no real device handle needed,
    //    exercises PostCompletion() + RunOnce()'s dispatch path for real.
    CompletionState completionState;
    IoCompletion completion{};
    completion.Pointer = &completionState;
    completion.callback = [](IoCompletion* self, DWORD bytesTransferred, ULONG_PTR completionKey, bool succeeded, DWORD /*errorCode*/) noexcept
    {
        auto* state = static_cast<CompletionState*>(self->Pointer);
        state->fired = true;
        state->bytesTransferred = bytesTransferred;
        state->completionKey = completionKey;
        state->succeeded = succeeded;
    };

    Result<void> postResult = loop.PostCompletion(&completion, 7, 42);
    CHECK(!postResult.HasError());

    Result<void> runOnceResult = loop.RunOnce();
    CHECK(!runOnceResult.HasError());
    CHECK(completionState.fired);
    CHECK(completionState.bytesTransferred == 7);
    CHECK(completionState.completionKey == 42);
    CHECK(completionState.succeeded);

    std::printf(
        "IocpSmokeTest: posted completion fired (bytes=%lu, key=%zu, ok=%d)\n",
        static_cast<unsigned long>(completionState.bytesTransferred),
        static_cast<size_t>(completionState.completionKey),
        completionState.succeeded ? 1 : 0);

    // 2) A repeating timer, driven through Run(), stopping itself after
    //    3 firings — exercises ScheduleTimer(), the timer-driven wait
    //    timeout in RunOnce(), and RequestStop() together, against the
    //    real OS this time (not the mock from IocpLoopTest.cpp).
    TimerState timerState;
    timerState.loop = &loop;

    Result<TimerId> timerResult = loop.ScheduleTimer(
        10, /*repeat=*/true,
        [](void* userData) noexcept
        {
            auto* state = static_cast<TimerState*>(userData);
            ++state->fireCount;
            std::printf("IocpSmokeTest: timer fired (count=%d)\n", state->fireCount);
            if (state->fireCount >= 3)
            {
                state->loop->RequestStop();
            }
        },
        &timerState);
    CHECK(timerResult.HasValue());

    Result<void> runResult = loop.Run();
    CHECK(!runResult.HasError());
    CHECK(timerState.fireCount == 3);

    if (g_failures == 0)
    {
        std::printf("IocpSmokeTest: all checks passed\n");
        return 0;
    }

    std::printf("IocpSmokeTest: %d CHECK(S) FAILED\n", g_failures);
    return 1;
}
