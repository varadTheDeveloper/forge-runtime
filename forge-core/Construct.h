#pragma once

#include <new>
#include <utility>

namespace forge::core::detail
{

/// C++17-compatible replacement for std::construct_at (C++20, <memory>).
///
/// Placement-news a T at `location`, forwarding `args` to its constructor,
/// and returns `location`. Gecko's own build of this codebase (see
/// HISTORY.md) does not compile in full C++20 mode, so std::construct_at
/// is unavailable even though the language-level placement new it wraps
/// is plain C++11. Not constexpr (unlike std::construct_at) — nothing in
/// this codebase constructs a Result<T>/Vector<T>/UniquePtr<T> element in
/// a constant-evaluated context, so this costs nothing in practice.
template<typename T, typename... Args>
T* ConstructAt(T* location, Args&&... args)
{
    return ::new (static_cast<void*>(location)) T(std::forward<Args>(args)...);
}

} // namespace forge::core::detail
