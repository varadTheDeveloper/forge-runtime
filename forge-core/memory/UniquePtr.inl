#pragma once

#include <memory>
#include <utility>

namespace forge::core::memory
{

//==============================================================================
// Constructors
//==============================================================================

template<typename T>
constexpr UniquePtr<T>::UniquePtr() noexcept
    : pointer_(nullptr),
      allocator_(nullptr)
{
}

template<typename T>
constexpr UniquePtr<T>::UniquePtr(std::nullptr_t) noexcept
    : pointer_(nullptr),
      allocator_(nullptr)
{
}

template<typename T>
UniquePtr<T>::UniquePtr(
    T* pointer,
    Allocator& allocator) noexcept
    : pointer_(pointer),
      allocator_(&allocator)
{
}

//==============================================================================
// Move Constructor
//==============================================================================

template<typename T>
UniquePtr<T>::UniquePtr(
    UniquePtr&& other) noexcept
    : pointer_(std::exchange(other.pointer_, nullptr)),
      allocator_(std::exchange(other.allocator_, nullptr))
{
}

//==============================================================================
// Move Assignment
//==============================================================================

template<typename T>
UniquePtr<T>&
UniquePtr<T>::operator=(UniquePtr&& other) noexcept
{
    if (this != &other)
    {
        Reset();

        pointer_ = std::exchange(other.pointer_, nullptr);
        allocator_ = std::exchange(other.allocator_, nullptr);
    }

    return *this;
}

//==============================================================================
// Destructor
//==============================================================================

template<typename T>
UniquePtr<T>::~UniquePtr() noexcept
{
    Reset();
}

//==============================================================================
// Observers
//==============================================================================

template<typename T>
constexpr T*
UniquePtr<T>::Get() const noexcept
{
    return pointer_;
}

template<typename T>
constexpr T&
UniquePtr<T>::operator*() const noexcept
{
    return *pointer_;
}

template<typename T>
constexpr T*
UniquePtr<T>::operator->() const noexcept
{
    return pointer_;
}

template<typename T>
constexpr UniquePtr<T>::operator bool() const noexcept
{
    return pointer_ != nullptr;
}

//==============================================================================
// Modifiers
//==============================================================================

template<typename T>
T*
UniquePtr<T>::Release() noexcept
{
    T* released = pointer_;

    pointer_ = nullptr;
    allocator_ = nullptr;

    return released;
}

template<typename T>
void
UniquePtr<T>::Reset() noexcept
{
    if (pointer_ == nullptr)
    {
        return;
    }

    std::destroy_at(pointer_);

    allocator_->Deallocate(pointer_, sizeof(T), alignof(T));

    pointer_ = nullptr;
    allocator_ = nullptr;
}

template<typename T>
void
UniquePtr<T>::Swap(
    UniquePtr& other) noexcept
{
    using std::swap;

    swap(pointer_, other.pointer_);
    swap(allocator_, other.allocator_);
}

//==============================================================================
// Non-Member Swap
//==============================================================================

template<typename T>
void
Swap(
    UniquePtr<T>& lhs,
    UniquePtr<T>& rhs) noexcept
{
    lhs.Swap(rhs);
}

} // namespace forge::core::memory