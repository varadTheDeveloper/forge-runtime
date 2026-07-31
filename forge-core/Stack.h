#pragma once

#include "memory/Vector.h"
#include "memory/ResultVoid.h"
#include "Types.h"

namespace forge::core
{

/// LIFO adapter over Vector<T> — Stack owns no storage logic of its own,
/// it just restricts Vector's API down to push/pop/top at one end. Kept
/// as a distinct type rather than telling callers to "just use
/// Vector<T>::PushBack/PopBack/Back" because the restricted API documents
/// intent at the call site and rules out accidental random-access/
/// front-of-container use on something that's conceptually a stack.
template<typename T>
class [[nodiscard]] Stack
{
public:

    using SizeType = forge::core::Size;

    Stack() = default;

    explicit Stack(
        memory::Allocator& allocator);

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
    T& Top() noexcept;

    [[nodiscard]]
    const T& Top() const noexcept;

    //==========================================================================
    // Modifiers
    //==========================================================================

    Result<void> Push(
        const T& value);

    Result<void> Push(
        T&& value);

    template<typename... Args>
    Result<void> Emplace(
        Args&&... args);

    void Pop() noexcept;

    void Swap(
        Stack& other) noexcept;

private:

    Vector<T> storage_;
};

} // namespace forge::core

#include "Stack.inl"
