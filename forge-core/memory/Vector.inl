#pragma once

#include <memory>
#include <utility>

#include "../Construct.h"

namespace forge::core
{

//==============================================================================
// Helpers
//==============================================================================

template<typename T>
constexpr typename Vector<T>::SizeType
Vector<T>::AllocationSize(
    SizeType capacity) noexcept
{
    return capacity * sizeof(T);
}

template<typename T>
typename Vector<T>::SizeType
Vector<T>::NextCapacity() const noexcept
{
    return capacity_ == 0 ? 1 : capacity_ * 2;
}

//==============================================================================
// Construction
//==============================================================================

template<typename T>
Vector<T>::Vector()
    :
    data_(nullptr),
    size_(0),
    capacity_(0),
    allocator_(&memory::GetDefaultAllocator())
{
}

template<typename T>
Vector<T>::Vector(
    memory::Allocator& allocator)
    :
    data_(nullptr),
    size_(0),
    capacity_(0),
    allocator_(&allocator)
{
}

template<typename T>
Vector<T>::~Vector() noexcept
{
    Clear();

    if (data_ != nullptr)
    {
        allocator_->Deallocate(
            data_,
            AllocationSize(capacity_),
            alignof(T));
    }
}

//==============================================================================
// Capacity
//==============================================================================

template<typename T>
typename Vector<T>::SizeType
Vector<T>::Size() const noexcept
{
    return size_;
}

template<typename T>
typename Vector<T>::SizeType
Vector<T>::Capacity() const noexcept
{
    return capacity_;
}

template<typename T>
bool Vector<T>::Empty() const noexcept
{
    return size_ == 0;
}

//==============================================================================
// Element Access
//==============================================================================

template<typename T>
T* Vector<T>::Data() noexcept
{
    return data_;
}

template<typename T>
const T* Vector<T>::Data() const noexcept
{
    return data_;
}

//==============================================================================
// Capacity
//==============================================================================

template<typename T>
void Vector<T>::Clear() noexcept
{
    while (size_ > 0)
    {
        --size_;
        std::destroy_at(data_ + size_);
    }
}

//==============================================================================
// Element Access
//==============================================================================

template<typename T>
T& Vector<T>::operator[](
    SizeType index) noexcept
{
    return data_[index];
}

template<typename T>
const T& Vector<T>::operator[](
    SizeType index) const noexcept
{
    return data_[index];
}

template<typename T>
T& Vector<T>::Front() noexcept
{
    return data_[0];
}

template<typename T>
const T& Vector<T>::Front() const noexcept
{
    return data_[0];
}

template<typename T>
T& Vector<T>::Back() noexcept
{
    return data_[size_ - 1];
}

template<typename T>
const T& Vector<T>::Back() const noexcept
{
    return data_[size_ - 1];
}

//==============================================================================
// Capacity
//==============================================================================

template<typename T>
Result<void> Vector<T>::Reserve(
    SizeType capacity)
{
    if (capacity <= capacity_)
    {
        return {};
    }

    void* memory = allocator_->Allocate(
        AllocationSize(capacity),
        alignof(T));

    if (memory == nullptr)
    {
        return Result<void>(
            Failure{ Error(ErrorCode::OutOfMemory) });
    }

    T* newData = static_cast<T*>(memory);

    for (SizeType index = 0; index < size_; ++index)
    {
        forge::core::detail::ConstructAt(
            newData + index,
            std::move_if_noexcept(data_[index]));
    }

    for (SizeType index = size_; index > 0; --index)
    {
        std::destroy_at(data_ + (index - 1));
    }

    if (data_ != nullptr)
    {
        allocator_->Deallocate(
            data_,
            AllocationSize(capacity_),
            alignof(T));
    }

    data_ = newData;
    capacity_ = capacity;

    return {};
}

template<typename T>
Result<void> Vector<T>::GrowIfNeeded()
{
    if (size_ < capacity_)
    {
        return {};
    }

    return Reserve(
        NextCapacity());
}



//==============================================================================
// Modifiers
//==============================================================================

template<typename T>
Result<void> Vector<T>::PushBack(
    const T& value)
{
    if (Result<void> result = GrowIfNeeded(); result.HasError())
    {
        return result;
    }

    forge::core::detail::ConstructAt(
        data_ + size_,
        value);

    ++size_;

    return {};
}

template<typename T>
Result<void> Vector<T>::PushBack(
    T&& value)
{
    if (Result<void> result = GrowIfNeeded(); result.HasError())
    {
        return result;
    }

    forge::core::detail::ConstructAt(
        data_ + size_,
        std::move(value));

    ++size_;

    return {};
}

template<typename T>
template<typename... Args>
Result<void> Vector<T>::EmplaceBack(
    Args&&... args)
{
    if (Result<void> result = GrowIfNeeded(); result.HasError())
    {
        return result;
    }

    forge::core::detail::ConstructAt(
        data_ + size_,
        std::forward<Args>(args)...);

    ++size_;

    return {};
}

template<typename T>
void Vector<T>::PopBack() noexcept
{
    if (size_ == 0)
    {
        return;
    }

    --size_;

    std::destroy_at(
        data_ + size_);
}

template<typename T>
void Vector<T>::Swap(
    Vector& other) noexcept
{
    using std::swap;

    swap(data_, other.data_);
    swap(size_, other.size_);
    swap(capacity_, other.capacity_);
    swap(allocator_, other.allocator_);
}

//==============================================================================
// Construction
//==============================================================================

template<typename T>
Vector<T>::Vector(
    const Vector& other)
    :
    Vector(*other.allocator_)
{
    if (other.capacity_ > 0)
    {
        Reserve(other.capacity_).Ignore();
    }

    // If the Reserve() above failed to allocate (out of memory), capacity_
    // stays at 0. Bound the copy to whatever capacity was actually secured
    // instead of writing past an empty/undersized buffer.
    const SizeType count = capacity_ < other.size_ ? capacity_ : other.size_;

    for (SizeType index = 0; index < count; ++index)
    {
        forge::core::detail::ConstructAt(
            data_ + index,
            other.data_[index]);
    }

    size_ = count;
}

template<typename T>
Vector<T>::Vector(
    Vector&& other) noexcept
    :
    data_(other.data_),
    size_(other.size_),
    capacity_(other.capacity_),
    allocator_(other.allocator_)
{
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

//==============================================================================
// Assignment
//==============================================================================

template<typename T>
Vector<T>& Vector<T>::operator=(
    const Vector& other)
{
    if (this == &other)
    {
        return *this;
    }

    Clear();

    if (capacity_ < other.size_)
    {
        Reserve(other.capacity_).Ignore();
    }

    // Same reasoning as the copy constructor: don't trust that Reserve()
    // above actually grew capacity_ far enough before copying into it.
    const SizeType count = capacity_ < other.size_ ? capacity_ : other.size_;

    for (SizeType index = 0; index < count; ++index)
    {
        forge::core::detail::ConstructAt(
            data_ + index,
            other.data_[index]);
    }

    size_ = count;

    return *this;
}

template<typename T>
Vector<T>& Vector<T>::operator=(
    Vector&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    Clear();

    if (data_ != nullptr)
    {
        allocator_->Deallocate(
            data_,
            AllocationSize(capacity_),
            alignof(T));
    }

    data_ = other.data_;
    size_ = other.size_;
    capacity_ = other.capacity_;
    allocator_ = other.allocator_;

    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;

    return *this;
}

} // namespace forge::core
// NOTE: everything above this line must stay inside namespace forge::core.
// (This file previously closed the namespace right after Data(), silently
// leaving Clear/[]/Front/Back/Reserve/GrowIfNeeded/PushBack/EmplaceBack/
// PopBack/Swap and the copy/move ctors and assignment operators defined at
// global scope, which does not compile since Vector<T> is a forge::core
// member — the same class of bug that was in Result.inl.)