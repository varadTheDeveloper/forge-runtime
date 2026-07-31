#pragma once

#include "Span.h"
#include "Types.h"

namespace forge::core
{

/// Non-owning view over a UTF-8 byte range. Never allocates. Not
/// guaranteed null-terminated — a StringView can be a substring of a
/// larger buffer, so treat `Data()` as exactly `Size()` bytes, nothing
/// past it.
///
/// Built on Span<const char> rather than duplicating its storage/bounds
/// logic — this is purely the string-specific vocabulary (comparison,
/// substr, find, starts/ends-with) layered on top.
class StringView
{
public:

    using SizeType = forge::core::Size;

    static constexpr SizeType kNotFound = static_cast<SizeType>(-1);

    constexpr StringView() noexcept = default;

    constexpr StringView(
        const char* data,
        SizeType size) noexcept
        :
        span_(data, size)
    {
    }

    // Intentionally not `explicit`: a string literal or any null-terminated
    // `const char*` should convert to a StringView as easily as it would to
    // a `const std::string&` parameter — that's the whole ergonomic point
    // of a view type. Computes the length via a plain scan (no <cstring>
    // dependency) so this stays usable even before Reserve()/Append() are
    // involved.
    constexpr StringView(
        const char* nullTerminated) noexcept
        :
        span_(nullTerminated, Length(nullTerminated))
    {
    }

    [[nodiscard]]
    constexpr const char* Data() const noexcept
    {
        return span_.Data();
    }

    [[nodiscard]]
    constexpr SizeType Size() const noexcept
    {
        return span_.Size();
    }

    [[nodiscard]]
    constexpr bool Empty() const noexcept
    {
        return span_.Empty();
    }

    [[nodiscard]]
    constexpr char operator[](
        SizeType index) const noexcept
    {
        return span_[index];
    }

    [[nodiscard]]
    constexpr StringView Substr(
        SizeType offset,
        SizeType count = kNotFound) const noexcept
    {
        const Span<const char> sub = span_.Subspan(offset, count);
        return StringView(sub.Data(), sub.Size());
    }

    [[nodiscard]]
    constexpr bool StartsWith(
        StringView prefix) const noexcept
    {
        if (prefix.Size() > Size())
        {
            return false;
        }

        return Substr(0, prefix.Size()) == prefix;
    }

    [[nodiscard]]
    constexpr bool EndsWith(
        StringView suffix) const noexcept
    {
        if (suffix.Size() > Size())
        {
            return false;
        }

        return Substr(Size() - suffix.Size(), suffix.Size()) == suffix;
    }

    /// Byte offset of the first occurrence of `needle`, or kNotFound.
    /// Empty `needle` matches at offset 0, same convention as
    /// std::string_view::find.
    [[nodiscard]]
    constexpr SizeType Find(
        StringView needle) const noexcept
    {
        if (needle.Size() > Size())
        {
            return kNotFound;
        }

        const SizeType lastStart = Size() - needle.Size();

        for (SizeType start = 0; start <= lastStart; ++start)
        {
            if (Substr(start, needle.Size()) == needle)
            {
                return start;
            }
        }

        return kNotFound;
    }

    /// Byte offset of the LAST occurrence of `needle`, or kNotFound.
    /// Added for Path's Parent()/FileName() (finding the final path
    /// separator) — StringView wasn't declared frozen anywhere the way
    /// Error is, so this is a backward-compatible addition, not a
    /// redesign of anything.
    [[nodiscard]]
    constexpr SizeType RFind(
        StringView needle) const noexcept
    {
        if (needle.Size() > Size())
        {
            return kNotFound;
        }

        SizeType start = Size() - needle.Size();

        while (true)
        {
            if (Substr(start, needle.Size()) == needle)
            {
                return start;
            }

            if (start == 0)
            {
                break;
            }

            --start;
        }

        return kNotFound;
    }

    [[nodiscard]]
    friend constexpr bool operator==(
        StringView lhs,
        StringView rhs) noexcept
    {
        if (lhs.Size() != rhs.Size())
        {
            return false;
        }

        for (SizeType index = 0; index < lhs.Size(); ++index)
        {
            if (lhs[index] != rhs[index])
            {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]]
    friend constexpr bool operator!=(
        StringView lhs,
        StringView rhs) noexcept
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr const char* begin() const noexcept { return span_.begin(); }
    [[nodiscard]] constexpr const char* end() const noexcept { return span_.end(); }

private:

    [[nodiscard]]
    static constexpr SizeType Length(
        const char* nullTerminated) noexcept
    {
        if (nullTerminated == nullptr)
        {
            return 0;
        }

        SizeType length = 0;

        while (nullTerminated[length] != '\0')
        {
            ++length;
        }

        return length;
    }

    Span<const char> span_;
};

} // namespace forge::core
