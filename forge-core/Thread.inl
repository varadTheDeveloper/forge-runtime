#pragma once

#include <utility>

namespace forge::core
{

template<typename Callable>
Result<Thread> Thread::Create(
    memory::Allocator& allocator,
    Callable callable)
{
    Result<detail::ErasedCallable> erased =
        detail::MakeErasedCallable(allocator, std::move(callable));

    if (erased.HasError())
    {
        return Result<Thread>(Failure{ erased.Error() });
    }

    Result<Thread> created = Thread::CreateWithErasedCallable(allocator, erased.Value());

    if (created.HasError())
    {
        // CreateWithErasedCallable failed after the closure was already
        // allocated (e.g. CreateThread itself failed) — destroy it
        // without running it, so we don't leak the allocation.
        erased.Value().destroy(erased.Value().closure);
        return created;
    }

    return created;
}

template<typename Callable>
Result<Thread> Thread::Create(
    Callable callable)
{
    return Create(memory::GetDefaultAllocator(), std::move(callable));
}

} // namespace forge::core
