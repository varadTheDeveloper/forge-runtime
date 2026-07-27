#pragma once

#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "../Construct.h"

namespace forge::core::memory
{

//==============================================================================
// MakeUnique (Default Allocator)
//==============================================================================

template<typename T, typename... Args>
[[nodiscard]]
Result<UniquePtr<T>>
MakeUnique(
    Args&&... args)
{
    return MakeUnique<T>(
        GetDefaultAllocator(),
        std::forward<Args>(args)...);
}

//==============================================================================
// MakeUnique (Custom Allocator)
//==============================================================================

template<typename T, typename... Args>
[[nodiscard]]
Result<UniquePtr<T>>
MakeUnique(
    Allocator& allocator,
    Args&&... args)
{
    static_assert(
        !std::is_array_v<T>,
        "MakeUnique<T[]> is not supported.");

    void* memory = allocator.Allocate(
        sizeof(T),
        alignof(T));

    if (memory == nullptr)
    {
        return Result<UniquePtr<T>>(
            Failure(Error(ErrorCode::OutOfMemory)));
    }

#if defined(__cpp_exceptions)

    try
    {
#endif

        T* object = forge::core::detail::ConstructAt(
            static_cast<T*>(memory),
            std::forward<Args>(args)...);

        return Result<UniquePtr<T>>(
            UniquePtr<T>(object, allocator));

#if defined(__cpp_exceptions)

    }
    catch (...)
    {
        allocator.Deallocate(
            memory,
            sizeof(T),
            alignof(T));

        throw;
    }

#endif
}

} // namespace forge::core::memory