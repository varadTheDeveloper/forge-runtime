#pragma once

#include <utility>

namespace forge::core
{

template<typename Callable>
Result<void> ThreadPool::Submit(
    Callable callable)
{
    if (allocator_ == nullptr)
    {
        return Result<void>(Failure{ Error(ErrorCode::InvalidOperation) });
    }

    Result<detail::ErasedCallable> erased =
        detail::MakeErasedCallable(*allocator_, std::move(callable));

    if (erased.HasError())
    {
        return Result<void>(Failure{ erased.Error() });
    }

    Result<void> submitted = SubmitErased(erased.Value());

    if (submitted.HasError())
    {
        // Queue was full/OOM, or the pool is shutting down — the closure
        // was allocated but never handed to a worker, so destroy it
        // without running it rather than leaking the allocation.
        erased.Value().destroy(erased.Value().closure);
    }

    return submitted;
}

} // namespace forge::core
