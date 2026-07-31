#pragma once

namespace forge::core
{

//==============================================================================
// Capacity
//==============================================================================

template<typename K>
typename HashSet<K>::SizeType
HashSet<K>::Size() const noexcept
{
    return table_.Size();
}

template<typename K>
typename HashSet<K>::SizeType
HashSet<K>::Capacity() const noexcept
{
    return table_.Capacity();
}

template<typename K>
bool HashSet<K>::Empty() const noexcept
{
    return table_.Empty();
}

template<typename K>
Result<void> HashSet<K>::Reserve(
    SizeType minSize)
{
    return table_.Reserve(minSize);
}

template<typename K>
void HashSet<K>::Clear() noexcept
{
    table_.Clear();
}

//==============================================================================
// Lookup
//==============================================================================

template<typename K>
bool HashSet<K>::Contains(
    const K& key) const noexcept
{
    return table_.Contains(key);
}

//==============================================================================
// Modifiers
//==============================================================================

template<typename K>
Result<bool> HashSet<K>::Insert(
    const K& key)
{
    return table_.Insert(key, detail::Unit{});
}

template<typename K>
bool HashSet<K>::Erase(
    const K& key) noexcept
{
    return table_.Erase(key);
}

template<typename K>
void HashSet<K>::Swap(
    HashSet& other) noexcept
{
    table_.Swap(other.table_);
}

//==============================================================================
// Iteration
//==============================================================================

template<typename K>
typename HashSet<K>::Iterator
HashSet<K>::begin() noexcept
{
    return Iterator(table_.begin());
}

template<typename K>
typename HashSet<K>::Iterator
HashSet<K>::end() noexcept
{
    return Iterator(table_.end());
}

} // namespace forge::core
