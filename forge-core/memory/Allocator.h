#pragma once

#include "../Types.h"

namespace forge::core::memory
{

/// Base interface for raw memory allocation.
///
/// Allocators are responsible only for allocating and deallocating
/// uninitialized memory. They do not construct or destroy objects.
///
/// Contract:
/// - Allocate() returns nullptr on failure.
/// - Allocate() never throws.
/// - Deallocate() accepts nullptr.
/// - The size and alignment passed to Deallocate() must match the
///   corresponding Allocate() call.
class Allocator
{
public:

    Allocator() = default;

    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;

    Allocator(Allocator&&) = delete;
    Allocator& operator=(Allocator&&) = delete;

    virtual ~Allocator() = default;

    [[nodiscard]]
    virtual void* Allocate(
        Size size,
        Size alignment) noexcept = 0;

    virtual void Deallocate(
        void* memory,
        Size size,
        Size alignment) noexcept = 0;
};

} // namespace forge::core::memory