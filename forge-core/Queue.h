#pragma once

#include "memory/ResultVoid.h"
#include "memory/Allocator.h"
#include "memory/DefaultAllocator.h"
#include "Types.h"

namespace forge::core
{

/// FIFO, Allocator-based, growable ring buffer. Deliberately not built on
/// top of Vector<T> the way Stack<T> is: Vector only ever grows/shrinks
/// at one end, so a Vector-backed queue would need an O(n) shift on every
/// Pop() (or leak the front slots forever). A circular buffer keeps both
/// Push() and Pop() O(1) (amortized for Push(), where growth applies),
/// same growth/Reserve/copy-truncates-on-OOM conventions as Vector<T>
/// otherwise.
template<typename T>
class [[nodiscard]] Queue
{
public:

    using SizeType = forge::core::Size;

    //==========================================================================
    // Construction
    //==========================================================================

    Queue() noexcept;

    explicit Queue(
        memory::Allocator& allocator) noexcept;

    Queue(
        const Queue& other);

    Queue(
        Queue&& other) noexcept;

    ~Queue() noexcept;

    //==========================================================================
    // Assignment
    //==========================================================================

    Queue& operator=(
        const Queue& other);

    Queue& operator=(
        Queue&& other) noexcept;

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
    T& Front() noexcept;

    [[nodiscard]]
    const T& Front() const noexcept;

    [[nodiscard]]
    T& Back() noexcept;

    [[nodiscard]]
    const T& Back() const noexcept;

    /// Logical, front-relative indexing: `operator[](0)` aliases the same
    /// element `Front()` does, `operator[](Size() - 1)` aliases the same
    /// element `Back()` does. Added in Phase 6 for callers (e.g. Forge's
    /// microtask queue) that need to walk every still-pending element
    /// in order without popping them — GC root tracing being the
    /// motivating case: a JobQueue must trace every queued microtask's
    /// callback, not just the front one, or the collector could reclaim
    /// a callback a later Pop() still needs. Same signature/`noexcept`
    /// convention as Vector<T>::operator[]; out-of-range `offset` is
    /// undefined behavior there and here alike (Vector/Queue trust the
    /// caller to have already checked Size(), matching every other
    /// unchecked-index accessor in this codebase).
    [[nodiscard]]
    T& operator[](
        SizeType offset) noexcept;

    [[nodiscard]]
    const T& operator[](
        SizeType offset) const noexcept;

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
        Queue& other) noexcept;

private:

    [[nodiscard]]
    static constexpr SizeType AllocationSize(
        SizeType capacity) noexcept;

    [[nodiscard]]
    SizeType NextCapacity() const noexcept;

    /// Logical index `offset` (0 == front) to its physical slot in `data_`.
    [[nodiscard]]
    SizeType PhysicalIndex(
        SizeType offset) const noexcept;

    Result<void> GrowIfNeeded();

private:

    T* data_{ nullptr };

    SizeType size_{ 0 };

    SizeType capacity_{ 0 };

    /// Physical index of the front (oldest) element. Ignored/meaningless
    /// while size_ == 0.
    SizeType head_{ 0 };

    memory::Allocator* allocator_{ &memory::GetDefaultAllocator() };
};

} // namespace forge::core

#include "Queue.inl"
