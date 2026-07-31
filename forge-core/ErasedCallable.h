#pragma once

#include <utility>

#include "Construct.h"
#include "Error.h"
#include "Result.h"
#include "Types.h"
#include "memory/Allocator.h"

namespace forge::core::detail
{

/// Type-erases an arbitrary no-argument Callable into a heap-allocated
/// closure plus a pair of plain (non-template) function pointers, so
/// OS APIs that only accept a `void(*)(void*)`-shaped callback — Win32's
/// `CreateThread` thread proc, or ThreadPool's internal task queue — can
/// invoke arbitrary user-supplied callables without dragging templates
/// (and therefore <windows.h>) into every header that wants to hand work
/// to a thread. `Thread::Create` and `ThreadPool::Submit` both use this
/// instead of duplicating the same erasure dance.
///
/// Lifecycle: exactly one of `invoke` or `destroy` must be called on the
/// returned `closure`, exactly once. `invoke` runs the callable and then
/// destroys/frees the closure; `destroy` frees it without running the
/// callable (the cleanup path for when the closure was created but never
/// successfully handed off anywhere — e.g. `CreateThread` itself failed
/// after the closure was already allocated).
struct ErasedCallable
{
    void* closure;
    void (*invoke)(void*) noexcept;
    void (*destroy)(void*) noexcept;
};

template<typename Callable>
struct ErasedClosureStorage
{
    memory::Allocator* allocator;
    Callable callable;
};

template<typename Callable>
void InvokeAndDestroyErasedCallable(
    void* rawClosure) noexcept
{
    using StorageType = ErasedClosureStorage<Callable>;

    StorageType* storage = static_cast<StorageType*>(rawClosure);
    memory::Allocator* allocator = storage->allocator;

    // If Callable::operator() throws in a build where exceptions are
    // enabled, that propagates out of this noexcept function and
    // terminates — the same outcome std::thread's own contract gives an
    // exception that escapes a thread's initial function. Forge-core's
    // real build has exceptions disabled anyway, so this is purely a
    // "be consistent with std:: precedent" note, not a live concern here.
    storage->callable();

    storage->~StorageType();
    allocator->Deallocate(storage, sizeof(StorageType), alignof(StorageType));
}

template<typename Callable>
void DestroyErasedCallableWithoutInvoking(
    void* rawClosure) noexcept
{
    using StorageType = ErasedClosureStorage<Callable>;

    StorageType* storage = static_cast<StorageType*>(rawClosure);
    memory::Allocator* allocator = storage->allocator;

    storage->~StorageType();
    allocator->Deallocate(storage, sizeof(StorageType), alignof(StorageType));
}

/// Allocates and constructs the closure. On success, the caller owns the
/// returned ErasedCallable and must eventually call exactly one of
/// `invoke`/`destroy` on it (see ErasedCallable's own comment). On
/// failure (OOM), nothing was allocated — there is nothing to clean up.
template<typename Callable>
[[nodiscard]]
Result<ErasedCallable> MakeErasedCallable(
    memory::Allocator& allocator,
    Callable callable)
{
    using StorageType = ErasedClosureStorage<Callable>;

    void* raw = allocator.Allocate(sizeof(StorageType), alignof(StorageType));

    if (raw == nullptr)
    {
        return Result<ErasedCallable>(Failure{ Error(ErrorCode::OutOfMemory) });
    }

#if defined(__cpp_exceptions)

    try
    {
#endif

        StorageType* storage = ConstructAt<StorageType>(
            static_cast<StorageType*>(raw),
            StorageType{ &allocator, std::move(callable) });

        return Result<ErasedCallable>(ErasedCallable{
            storage,
            &InvokeAndDestroyErasedCallable<Callable>,
            &DestroyErasedCallableWithoutInvoking<Callable> });

#if defined(__cpp_exceptions)

    }
    catch (...)
    {
        allocator.Deallocate(raw, sizeof(StorageType), alignof(StorageType));
        throw;
    }

#endif
}

} // namespace forge::core::detail
