#pragma once

#include "Result.h"
#include "memory/ResultVoid.h"
#include "memory/Allocator.h"
#include "memory/DefaultAllocator.h"
#include "StringView.h"
#include "Types.h"

namespace forge::core
{

/// Owning, growable UTF-8 byte buffer, built on memory::Allocator — the
/// same convention as Vector<T>, not std::allocator/new. Constructors
/// never allocate (default-constructed String is empty, no allocation);
/// anything that needs to allocate returns Result<void> (Reserve, Append)
/// or, for constructing a String that already contains content, goes
/// through the fallible String::Create(...) factory below, per the
/// decision recorded in ROADMAP.md's Phase 1 entry.
///
/// Always null-terminated internally (CStr() is always valid, even on a
/// default-constructed/empty String) for interop with C/C-style APIs —
/// this is the one place String's internals differ from Vector<char>
/// rather than just being Vector<char> with extra methods.
class [[nodiscard]] String
{
public:

    using SizeType = forge::core::Size;

    //==========================================================================
    // Construction
    //==========================================================================

    String() noexcept;

    explicit String(
        memory::Allocator& allocator) noexcept;

    String(
        const String& other);

    String(
        String&& other) noexcept;

    ~String() noexcept;

    /// Allocating factory for a String that already contains `initial`'s
    /// content — the fallible counterpart to the non-allocating default
    /// constructor. Prefer this over `String s; s.Append(initial).Ignore();`
    /// when the append failing should actually be checked.
    [[nodiscard]]
    static Result<String> Create(
        memory::Allocator& allocator,
        StringView initial);

    [[nodiscard]]
    static Result<String> Create(
        StringView initial);

    //==========================================================================
    // Assignment
    //==========================================================================

    String& operator=(
        const String& other);

    String& operator=(
        String&& other) noexcept;

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
    char& operator[](
        SizeType index) noexcept;

    [[nodiscard]]
    const char& operator[](
        SizeType index) const noexcept;

    [[nodiscard]]
    char* Data() noexcept;

    [[nodiscard]]
    const char* Data() const noexcept;

    /// Always a valid, null-terminated pointer — safe to hand to a C API
    /// even on a default-constructed (empty, never-allocated) String.
    [[nodiscard]]
    const char* CStr() const noexcept;

    [[nodiscard]]
    StringView View() const noexcept;

    // Not `explicit`: a String should convert to a StringView as freely as
    // it converts to `const char*` via CStr() — this is what lets a
    // String be passed anywhere a StringView parameter (Append,
    // StartsWith, ==, ...) is expected without spelling `.View()` at every
    // call site.
    [[nodiscard]]
    operator StringView() const noexcept;

    //==========================================================================
    // Modifiers
    //==========================================================================

    Result<void> Append(
        char c);

    Result<void> Append(
        StringView text);

    void Swap(
        String& other) noexcept;

    //==========================================================================
    // Comparison
    //==========================================================================

    // Declared as hidden friends directly on String rather than relying on
    // the implicit String -> StringView conversion plus StringView's own
    // hidden-friend operator==: that would NOT actually work. A hidden
    // friend (one declared only inside a class body, with no matching
    // declaration at namespace scope) is only pulled into overload
    // resolution by ADL when the class that declares it is literally one
    // of the argument types of the call. `String == String` has no
    // argument of type StringView at all, so StringView is never among
    // the associated classes and its friend operator== is invisible —
    // confirmed by an actual compile failure ("no match for operator==")
    // during verification, not just theory. These three overloads cover
    // String==String, String==StringView, and StringView==String; a
    // String==`const char*` comparison still works because the `const
    // char*`/literal argument has no class type to constrain ADL, so
    // these hidden friends of String are found via the *other* (String)
    // argument, and the literal converts to StringView same as before.
    [[nodiscard]]
    friend bool operator==(
        const String& lhs,
        const String& rhs) noexcept
    {
        return lhs.View() == rhs.View();
    }

    [[nodiscard]]
    friend bool operator!=(
        const String& lhs,
        const String& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    [[nodiscard]]
    friend bool operator==(
        const String& lhs,
        StringView rhs) noexcept
    {
        return lhs.View() == rhs;
    }

    [[nodiscard]]
    friend bool operator!=(
        const String& lhs,
        StringView rhs) noexcept
    {
        return !(lhs == rhs);
    }

    [[nodiscard]]
    friend bool operator==(
        StringView lhs,
        const String& rhs) noexcept
    {
        return lhs == rhs.View();
    }

    [[nodiscard]]
    friend bool operator!=(
        StringView lhs,
        const String& rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:

    [[nodiscard]]
    static constexpr SizeType AllocationSize(
        SizeType capacity) noexcept;

    [[nodiscard]]
    SizeType NextCapacity() const noexcept;

    Result<void> GrowIfNeeded();

private:

    char* data_{ nullptr };

    SizeType size_{ 0 };

    SizeType capacity_{ 0 };

    memory::Allocator* allocator_{ &memory::GetDefaultAllocator() };
};

} // namespace forge::core

#include "String.inl"
