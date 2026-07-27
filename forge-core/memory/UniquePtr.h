#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "Allocator.h"

namespace forge::core::memory
{

template<typename T>
class UniquePtr final
{
    static_assert(
        !std::is_array_v<T>,
        "UniquePtr<T[]> is not supported.");

public:

    using ValueType = T;

public:

    constexpr UniquePtr() noexcept;

    constexpr UniquePtr(std::nullptr_t) noexcept;

    UniquePtr(
        T* pointer,
        Allocator& allocator) noexcept;

    UniquePtr(UniquePtr&& other) noexcept;

    UniquePtr& operator=(UniquePtr&& other) noexcept;

    ~UniquePtr() noexcept;

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

public:

    [[nodiscard]]
    constexpr T* Get() const noexcept;

    [[nodiscard]]
    constexpr T& operator*() const noexcept;

    [[nodiscard]]
    constexpr T* operator->() const noexcept;

    [[nodiscard]]
    constexpr explicit operator bool() const noexcept;

public:

    [[nodiscard]]
    T* Release() noexcept;

    void Reset() noexcept;

    void Swap(UniquePtr& other) noexcept;

private:

    T* pointer_ = nullptr;

    Allocator* allocator_ = nullptr;
};

template<typename T>
void Swap(
    UniquePtr<T>& lhs,
    UniquePtr<T>& rhs) noexcept;

} // namespace forge::core::memory

#include "UniquePtr.inl"