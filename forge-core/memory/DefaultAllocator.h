#pragma once

#include "Allocator.h"

namespace forge::core::memory
{

/// Default allocator implementation.
///
/// This allocator forwards allocation requests to the platform's
/// default allocation mechanism. It is stateless and may be shared
/// across the entire application.
///
/// Obtain the global instance through GetDefaultAllocator().
class DefaultAllocator final : public Allocator
{
public:

    DefaultAllocator() = default;

    DefaultAllocator(const DefaultAllocator&) = delete;
    DefaultAllocator& operator=(const DefaultAllocator&) = delete;

    DefaultAllocator(DefaultAllocator&&) = delete;
    DefaultAllocator& operator=(DefaultAllocator&&) = delete;

    ~DefaultAllocator() override = default;

    [[nodiscard]]
    void* Allocate(
        Size size,
        Size alignment) noexcept override;

    void Deallocate(
        void* memory,
        Size size,
        Size alignment) noexcept override;
};

/// Returns the process-wide default allocator.
///
/// The returned instance is lazily initialized and remains valid
/// until program termination.
///
/// Thread-safe initialization is guaranteed by C++11.
[[nodiscard]]
Allocator& GetDefaultAllocator() noexcept;

} // namespace forge::core::memory