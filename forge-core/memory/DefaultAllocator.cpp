#include "DefaultAllocator.h"

#include "detail/AllocationBackend.h"

namespace forge::core::memory
{

void* DefaultAllocator::Allocate(
    Size size,
    Size alignment) noexcept
{
    return detail::Allocate(
        size,
        alignment);
}

void DefaultAllocator::Deallocate(
    void* memory,
    Size,
    Size alignment) noexcept
{
    detail::Deallocate(
        memory,
        alignment);
}

Allocator& GetDefaultAllocator() noexcept
{
    static DefaultAllocator allocator;

    return allocator;
}

} // namespace forge::core::memory