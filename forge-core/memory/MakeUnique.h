#pragma once

#include "../Result.h"

#include "DefaultAllocator.h"
#include "UniquePtr.h"

namespace forge::core::memory
{

template<typename T, typename... Args>
[[nodiscard]]
Result<UniquePtr<T>>
MakeUnique(Args&&... args);

template<typename T, typename... Args>
[[nodiscard]]
Result<UniquePtr<T>>
MakeUnique(
    Allocator& allocator,
    Args&&... args);

}

#include "MakeUnique.inl"