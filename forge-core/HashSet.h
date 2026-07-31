#pragma once

#include "HashMap.h"
#include "memory/ResultVoid.h"
#include "memory/Allocator.h"
#include "Types.h"

namespace forge::core
{

/// Hash set, implemented as a thin wrapper around HashMap<K, detail::Unit>
/// rather than a second, independent open-addressing implementation — see
/// HashMap.h for the actual table logic (probing, tombstones, growth,
/// copy-truncates-on-OOM). Keeping this a composition rather than a
/// parallel copy-pasted table means any future fix/improvement to
/// HashMap's probing logic applies to HashSet for free.
template<typename K>
class [[nodiscard]] HashSet
{
public:

    using SizeType = forge::core::Size;

    /// Wraps HashMap<K, detail::Unit>::Iterator, exposing only the key —
    /// a set has no per-entry value to hand out.
    class Iterator
    {
    public:

        Iterator& operator++() noexcept
        {
            ++inner_;
            return *this;
        }

        [[nodiscard]]
        const K& operator*() const noexcept
        {
            return (*inner_).Key();
        }

        [[nodiscard]]
        bool operator==(
            const Iterator& other) const noexcept
        {
            return inner_ == other.inner_;
        }

        [[nodiscard]]
        bool operator!=(
            const Iterator& other) const noexcept
        {
            return !(*this == other);
        }

    private:

        friend class HashSet;

        explicit Iterator(
            typename HashMap<K, detail::Unit>::Iterator inner) noexcept
            :
            inner_(inner)
        {
        }

        typename HashMap<K, detail::Unit>::Iterator inner_;
    };

    //==========================================================================
    // Construction
    //==========================================================================

    HashSet() noexcept = default;

    explicit HashSet(
        memory::Allocator& allocator) noexcept
        :
        table_(allocator)
    {
    }

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
        SizeType minSize);

    void Clear() noexcept;

    //==========================================================================
    // Lookup
    //==========================================================================

    [[nodiscard]]
    bool Contains(
        const K& key) const noexcept;

    //==========================================================================
    // Modifiers
    //==========================================================================

    /// Returns true if `key` was newly inserted, false if it was already
    /// present (a HashSet has no value to replace, so unlike
    /// HashMap::Insert there is no "replaced" case).
    Result<bool> Insert(
        const K& key);

    bool Erase(
        const K& key) noexcept;

    void Swap(
        HashSet& other) noexcept;

    //==========================================================================
    // Iteration
    //==========================================================================

    [[nodiscard]]
    Iterator begin() noexcept;

    [[nodiscard]]
    Iterator end() noexcept;

private:

    HashMap<K, detail::Unit> table_;
};

} // namespace forge::core

#include "HashSet.inl"
