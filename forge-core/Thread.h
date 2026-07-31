#pragma once

#include <utility>

#include "ErasedCallable.h"
#include "Error.h"
#include "Result.h"
#include "Types.h"
#include "memory/Allocator.h"
#include "memory/DefaultAllocator.h"
#include "memory/ResultVoid.h"

namespace forge::core
{

/// A single OS thread running an arbitrary no-argument Callable to
/// completion. Win32-only backend (CreateThread) — see Thread.md for
/// the full spec.
///
/// Move-only, like File/UniquePtr — a running OS thread has no
/// well-defined "copy" operation.
///
/// Every Thread that was ever successfully created (Joinable() == true)
/// MUST be Join()'d or Detach()'d before it is destroyed or overwritten
/// by move-assignment — same contract std::thread has, enforced here
/// the same way: with an assertion, not silent behavior. Silently
/// detaching (like some other languages' thread types do) or silently
/// blocking to join both hide what is almost always a real bug — the
/// caller forgot to decide what should happen to this thread — rather
/// than surfacing it. See Thread.md's Non-Goals.
class [[nodiscard]] Thread
{
public:

    Thread() noexcept;

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    Thread(Thread&& other) noexcept;
    Thread& operator=(Thread&& other) noexcept;

    ~Thread() noexcept;

    /// Spawns a new thread that runs `callable()` to completion and then
    /// exits. `callable` is moved into a closure allocated from
    /// `allocator`, which stays alive until the spawned thread finishes
    /// running it — `allocator` itself must outlive the thread.
    template<typename Callable>
    [[nodiscard]]
    static Result<Thread> Create(
        memory::Allocator& allocator,
        Callable callable);

    template<typename Callable>
    [[nodiscard]]
    static Result<Thread> Create(
        Callable callable);

    [[nodiscard]]
    bool Joinable() const noexcept;

    /// Blocks the calling thread until this one finishes running its
    /// callable. Only fails for genuinely exceptional OS errors; calling
    /// this on a non-joinable Thread returns ErrorCode::InvalidOperation
    /// rather than asserting — unlike the destructor, a caller might
    /// reasonably check Joinable() first and branch, so this isn't
    /// necessarily a programming error the way destroying a joinable
    /// Thread unjoined is.
    Result<void> Join() noexcept;

    /// Releases ownership of the OS thread without waiting for it — it
    /// keeps running independently, and the OS reclaims its resources
    /// once it finishes. Joinable() is false after this call.
    void Detach() noexcept;

private:

    explicit Thread(
        void* handle) noexcept;

    // Implemented in Thread.cpp, where <windows.h> actually lives. Kept
    // as a plain (non-template) function so CreateThread itself is only
    // ever instantiated once, not once per Callable — only the closure
    // erasure in ErasedCallable.h is templated. Takes ownership of
    // `closure` on success; on failure, the caller (Create(), in
    // Thread.inl) is responsible for calling closure.destroy.
    [[nodiscard]]
    static Result<Thread> CreateWithErasedCallable(
        memory::Allocator& allocator,
        detail::ErasedCallable closure);

    // Kept as void* (an opaque Win32 HANDLE), same reasoning as File's
    // handle_ — <windows.h> stays confined to Thread.cpp.
    void* handle_{ nullptr };
};

} // namespace forge::core

#include "Thread.inl"
