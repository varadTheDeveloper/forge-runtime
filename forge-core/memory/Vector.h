#pragma once

#include <utility>

#include "ResultVoid.h"
#include "../Types.h"
#include "Allocator.h"
#include "DefaultAllocator.h"

namespace forge::core
{

template<typename T>
class [[nodiscard]] Vector
{
public:

    using SizeType = forge::core::Size;

    //==========================================================================
    // Construction
    //==========================================================================

    Vector();

    explicit Vector(
        memory::Allocator& allocator);

    Vector(
        const Vector& other);

    Vector(
        Vector&& other) noexcept;

    ~Vector() noexcept;

    //==========================================================================
    // Assignment
    //==========================================================================

    Vector& operator=(
        const Vector& other);

    Vector& operator=(
        Vector&& other) noexcept;

    //==========================================================================
    // Capacity
    //==========================================================================

    [[nodiscard]]
    SizeType Size() const noexcept;

    [[nodiscard]]
    SizeType Capacity() const noexcept;

    [[nodiscard]]
    bool Empty() const noexcept;

    Result<void> Reserve(
        SizeType capacity);

    void Clear() noexcept;

    //==========================================================================
    // Element Access
    //==========================================================================

    [[nodiscard]]
    T& operator[](
        SizeType index) noexcept;

    [[nodiscard]]
    const T& operator[](
        SizeType index) const noexcept;

    [[nodiscard]]
    T& Front() noexcept;

    [[nodiscard]]
    const T& Front() const noexcept;

    [[nodiscard]]
    T& Back() noexcept;

    [[nodiscard]]
    const T& Back() const noexcept;

    [[nodiscard]]
    T* Data() noexcept;

    [[nodiscard]]
    const T* Data() const noexcept;

    //==========================================================================
    // Modifiers
    //==========================================================================

    Result<void> PushBack(
        const T& value);

    Result<void> PushBack(
        T&& value);

    template<typename... Args>
    Result<void> EmplaceBack(
        Args&&... args);

    void PopBack() noexcept;

    void Swap(
        Vector& other) noexcept;

private:

    //==========================================================================
    // Helpers
    //==========================================================================

    [[nodiscard]]
    static constexpr SizeType AllocationSize(
        SizeType capacity) noexcept;

    [[nodiscard]]
    SizeType NextCapacity() const noexcept;

    Result<void> GrowIfNeeded();

private:

    T* data_{ nullptr };

    SizeType size_{ 0 };

    SizeType capacity_{ 0 };

    memory::Allocator* allocator_{ &memory::GetDefaultAllocator() };
};

} // namespace forge::core

#include "Vector.inl"