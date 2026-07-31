#pragma once

#include <utility>

namespace forge::core
{

template<typename T>
Stack<T>::Stack(
    memory::Allocator& allocator)
    :
    storage_(allocator)
{
}

//==============================================================================
// Capacity
//==============================================================================

template<typename T>
typename Stack<T>::SizeType
Stack<T>::Size() const noexcept
{
    return storage_.Size();
}

template<typename T>
typename Stack<T>::SizeType
Stack<T>::Capacity() const noexcept
{
    return storage_.Capacity();
}

template<typename T>
bool Stack<T>::Empty() const noexcept
{
    return storage_.Empty();
}

template<typename T>
Result<void> Stack<T>::Reserve(
    SizeType capacity)
{
    return storage_.Reserve(capacity);
}

template<typename T>
void Stack<T>::Clear() noexcept
{
    storage_.Clear();
}

//==============================================================================
// Element Access
//==============================================================================

template<typename T>
T& Stack<T>::Top() noexcept
{
    return storage_.Back();
}

template<typename T>
const T& Stack<T>::Top() const noexcept
{
    return storage_.Back();
}

//==============================================================================
// Modifiers
//==============================================================================

template<typename T>
Result<void> Stack<T>::Push(
    const T& value)
{
    return storage_.PushBack(value);
}

template<typename T>
Result<void> Stack<T>::Push(
    T&& value)
{
    return storage_.PushBack(std::move(value));
}

template<typename T>
template<typename... Args>
Result<void> Stack<T>::Emplace(
    Args&&... args)
{
    return storage_.EmplaceBack(std::forward<Args>(args)...);
}

template<typename T>
void Stack<T>::Pop() noexcept
{
    storage_.PopBack();
}

template<typename T>
void Stack<T>::Swap(
    Stack& other) noexcept
{
    storage_.Swap(other.storage_);
}

} // namespace forge::core
