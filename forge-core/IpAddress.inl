#pragma once

#include <utility>

#include "Assert.h"

namespace forge::core
{

namespace detail
{

[[nodiscard]]
inline bool IsAsciiDigit(
    char c) noexcept
{
    return c >= '0' && c <= '9';
}

[[nodiscard]]
inline bool IsAsciiHexDigit(
    char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

[[nodiscard]]
inline u8 HexDigitValue(
    char c) noexcept
{
    if (c >= '0' && c <= '9')
    {
        return static_cast<u8>(c - '0');
    }

    if (c >= 'a' && c <= 'f')
    {
        return static_cast<u8>(c - 'a' + 10);
    }

    return static_cast<u8>(c - 'A' + 10); // caller already validated via IsAsciiHexDigit
}

/// Shared by IPv4 octet formatting and port formatting — avoids writing
/// the same "peel off decimal digits into a small stack buffer, then
/// reverse them" logic three times over (IPv4 octets, IPv6 hex groups,
/// port numbers).
[[nodiscard]]
inline Result<void> AppendDecimal(
    String& out,
    u32 value)
{
    char digits[10]; // u32's max value is 10 decimal digits
    Size digitCount = 0;

    if (value == 0)
    {
        digits[digitCount++] = '0';
    }
    else
    {
        while (value > 0)
        {
            digits[digitCount++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
    }

    char buffer[10];
    Size length = 0;

    while (digitCount > 0)
    {
        buffer[length++] = digits[--digitCount];
    }

    return out.Append(StringView(buffer, length));
}

[[nodiscard]]
inline Result<void> AppendLowerHex(
    String& out,
    u16 value)
{
    char digits[4]; // a 16-bit group is at most 4 hex digits
    Size digitCount = 0;

    if (value == 0)
    {
        digits[digitCount++] = '0';
    }
    else
    {
        while (value > 0)
        {
            const u16 nibble = static_cast<u16>(value & 0xF);
            digits[digitCount++] = static_cast<char>(
                nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('a' + (nibble - 10)));
            value = static_cast<u16>(value >> 4);
        }
    }

    char buffer[4];
    Size length = 0;

    while (digitCount > 0)
    {
        buffer[length++] = digits[--digitCount];
    }

    return out.Append(StringView(buffer, length));
}

/// One IPv4 octet: 1-3 decimal digits, value 0-255. Rejects a
/// multi-digit segment with a leading zero (e.g. "01", "007") — see
/// IpAddress.md's Non-Goals for why (the historical octal-vs-decimal
/// inet_aton ambiguity).
[[nodiscard]]
inline bool ParseV4Octet(
    StringView text,
    u8& out) noexcept
{
    if (text.Empty() || text.Size() > 3)
    {
        return false;
    }

    if (text.Size() > 1 && text[0] == '0')
    {
        return false;
    }

    u32 value = 0;

    for (Size i = 0; i < text.Size(); ++i)
    {
        if (!IsAsciiDigit(text[i]))
        {
            return false;
        }

        value = value * 10 + static_cast<u32>(text[i] - '0');
    }

    if (value > 255)
    {
        return false;
    }

    out = static_cast<u8>(value);
    return true;
}

/// Splits `text` on ':' into up to `maxGroups` 16-bit hex groups (1-4
/// hex digits each). Rejects any empty segment — a bare/trailing/
/// leading single ':' not part of a "::" (which the caller strips out
/// before calling this) is invalid IPv6.
[[nodiscard]]
inline bool ParseV6Groups(
    StringView text,
    u16* out,
    Size maxGroups,
    Size& outCount) noexcept
{
    outCount = 0;
    Size segmentStart = 0;

    for (Size i = 0; i <= text.Size(); ++i)
    {
        const bool atEnd = (i == text.Size());

        if (!atEnd && text[i] != ':')
        {
            continue;
        }

        const StringView segment = text.Substr(segmentStart, i - segmentStart);

        if (segment.Empty() || segment.Size() > 4)
        {
            return false;
        }

        u16 value = 0;

        for (Size j = 0; j < segment.Size(); ++j)
        {
            if (!IsAsciiHexDigit(segment[j]))
            {
                return false;
            }

            value = static_cast<u16>((value << 4) | HexDigitValue(segment[j]));
        }

        if (outCount >= maxGroups)
        {
            return false;
        }

        out[outCount++] = value;
        segmentStart = i + 1;
    }

    return true;
}

/// Longest run of consecutive zero groups, length >= 2 (a lone zero
/// group is never compressed — see RFC 5952). Ties keep the FIRST
/// (leftmost) run, per RFC 5952's canonical-form rule — the `>` (not
/// `>=`) comparison below only replaces the current best on a strictly
/// longer run.
struct ZeroRun
{
    Size start{ 0 };
    Size length{ 0 };
};

[[nodiscard]]
inline ZeroRun FindLongestZeroRun(
    const u16 groups[8]) noexcept
{
    ZeroRun best;
    Size i = 0;

    while (i < 8)
    {
        if (groups[i] != 0)
        {
            ++i;
            continue;
        }

        const Size start = i;

        while (i < 8 && groups[i] == 0)
        {
            ++i;
        }

        const Size length = i - start;

        if (length > best.length)
        {
            best = ZeroRun{ start, length };
        }
    }

    return best;
}

[[nodiscard]]
inline Result<u16> ParsePort(
    StringView text) noexcept
{
    if (text.Empty() || text.Size() > 5)
    {
        return Result<u16>(Failure{ Error(ErrorCode::ParseError) });
    }

    if (text.Size() > 1 && text[0] == '0')
    {
        return Result<u16>(Failure{ Error(ErrorCode::ParseError) });
    }

    u32 value = 0;

    for (Size i = 0; i < text.Size(); ++i)
    {
        if (!IsAsciiDigit(text[i]))
        {
            return Result<u16>(Failure{ Error(ErrorCode::ParseError) });
        }

        value = value * 10 + static_cast<u32>(text[i] - '0');
    }

    if (value > 65535)
    {
        return Result<u16>(Failure{ Error(ErrorCode::ParseError) });
    }

    return Result<u16>(static_cast<u16>(value));
}

} // namespace detail

//==============================================================================
// IpAddress
//==============================================================================

inline IpAddress IpAddress::V4(
    u8 a,
    u8 b,
    u8 c,
    u8 d) noexcept
{
    IpAddress result;
    result.version_ = IpVersion::V4;
    result.bytes_[0] = a;
    result.bytes_[1] = b;
    result.bytes_[2] = c;
    result.bytes_[3] = d;
    return result;
}

inline IpAddress IpAddress::V6(
    Span<const u8> bytes) noexcept
{
    FORGE_ASSERT(bytes.Size() == 16);

    IpAddress result;
    result.version_ = IpVersion::V6;

    for (Size i = 0; i < 16 && i < bytes.Size(); ++i)
    {
        result.bytes_[i] = bytes[i];
    }

    return result;
}

inline Span<const u8> IpAddress::Bytes() const noexcept
{
    return Span<const u8>(bytes_, IsV4() ? 4 : 16);
}

inline Result<IpAddress> IpAddress::ParseV4(
    StringView text) noexcept
{
    u8 octets[4]{};
    Size octetIndex = 0;
    Size segmentStart = 0;

    for (Size i = 0; i <= text.Size(); ++i)
    {
        const bool atEnd = (i == text.Size());

        if (!atEnd && text[i] != '.')
        {
            continue;
        }

        if (octetIndex >= 4)
        {
            return Result<IpAddress>(Failure{ Error(ErrorCode::ParseError) });
        }

        const StringView segment = text.Substr(segmentStart, i - segmentStart);

        if (!detail::ParseV4Octet(segment, octets[octetIndex]))
        {
            return Result<IpAddress>(Failure{ Error(ErrorCode::ParseError) });
        }

        ++octetIndex;
        segmentStart = i + 1;
    }

    if (octetIndex != 4)
    {
        return Result<IpAddress>(Failure{ Error(ErrorCode::ParseError) });
    }

    return Result<IpAddress>(IpAddress::V4(octets[0], octets[1], octets[2], octets[3]));
}

inline Result<IpAddress> IpAddress::ParseV6(
    StringView text) noexcept
{
    if (text.Empty())
    {
        return Result<IpAddress>(Failure{ Error(ErrorCode::ParseError) });
    }

    const StringView doubleColon("::");
    const StringView::SizeType firstDouble = text.Find(doubleColon);
    const StringView::SizeType lastDouble = text.RFind(doubleColon);
    const bool hasCompression = (firstDouble != StringView::kNotFound);

    if (hasCompression && firstDouble != lastDouble)
    {
        return Result<IpAddress>(Failure{ Error(ErrorCode::ParseError) }); // more than one "::"
    }

    StringView leftText = hasCompression ? text.Substr(0, firstDouble) : text;
    StringView rightText = hasCompression ? text.Substr(firstDouble + 2) : StringView();

    u16 leftGroups[8]{};
    Size leftCount = 0;
    u16 rightGroups[8]{};
    Size rightCount = 0;

    if (!leftText.Empty())
    {
        if (!detail::ParseV6Groups(leftText, leftGroups, 8, leftCount))
        {
            return Result<IpAddress>(Failure{ Error(ErrorCode::ParseError) });
        }
    }

    if (hasCompression && !rightText.Empty())
    {
        if (!detail::ParseV6Groups(rightText, rightGroups, 8, rightCount))
        {
            return Result<IpAddress>(Failure{ Error(ErrorCode::ParseError) });
        }
    }

    u16 groups[8]{};

    if (hasCompression)
    {
        if (leftCount + rightCount >= 8)
        {
            return Result<IpAddress>(Failure{ Error(ErrorCode::ParseError) }); // "::" must stand for >= 1 group
        }

        const Size missing = 8 - leftCount - rightCount;
        Size idx = 0;

        for (Size i = 0; i < leftCount; ++i)
        {
            groups[idx++] = leftGroups[i];
        }

        for (Size i = 0; i < missing; ++i)
        {
            groups[idx++] = 0;
        }

        for (Size i = 0; i < rightCount; ++i)
        {
            groups[idx++] = rightGroups[i];
        }
    }
    else
    {
        if (leftCount != 8)
        {
            return Result<IpAddress>(Failure{ Error(ErrorCode::ParseError) });
        }

        for (Size i = 0; i < 8; ++i)
        {
            groups[i] = leftGroups[i];
        }
    }

    u8 bytes[16];

    for (Size i = 0; i < 8; ++i)
    {
        bytes[i * 2] = static_cast<u8>(groups[i] >> 8);
        bytes[i * 2 + 1] = static_cast<u8>(groups[i] & 0xFF);
    }

    return Result<IpAddress>(IpAddress::V6(Span<const u8>(bytes, 16)));
}

inline Result<IpAddress> IpAddress::Parse(
    StringView text) noexcept
{
    // IPv4 dotted-quad text never contains ':'; IPv6 colon-hex text
    // always does — a single character is enough to route to the right
    // parser without any lookahead.
    if (text.Find(":") != StringView::kNotFound)
    {
        return ParseV6(text);
    }

    return ParseV4(text);
}

inline Result<void> IpAddress::AppendV4String(
    String& out) const
{
    for (Size i = 0; i < 4; ++i)
    {
        if (i > 0)
        {
            if (Result<void> appended = out.Append("."); appended.HasError())
            {
                return appended;
            }
        }

        if (Result<void> appended = detail::AppendDecimal(out, bytes_[i]); appended.HasError())
        {
            return appended;
        }
    }

    return {};
}

inline Result<void> IpAddress::AppendV6String(
    String& out) const
{
    u16 groups[8];

    for (Size i = 0; i < 8; ++i)
    {
        groups[i] = static_cast<u16>(
            (static_cast<u16>(bytes_[i * 2]) << 8) | bytes_[i * 2 + 1]);
    }

    const detail::ZeroRun run = detail::FindLongestZeroRun(groups);
    const bool compress = run.length >= 2;

    bool first = true;

    for (Size i = 0; i < 8; )
    {
        if (compress && i == run.start)
        {
            if (Result<void> appended = out.Append("::"); appended.HasError())
            {
                return appended;
            }

            i += run.length;
            first = true; // "::" already serves as the separator for the next group
            continue;
        }

        if (!first)
        {
            if (Result<void> appended = out.Append(":"); appended.HasError())
            {
                return appended;
            }
        }

        if (Result<void> appended = detail::AppendLowerHex(out, groups[i]); appended.HasError())
        {
            return appended;
        }

        first = false;
        ++i;
    }

    return {};
}

inline Result<String> IpAddress::ToString(
    memory::Allocator& allocator) const
{
    String result(allocator);

    Result<void> appended = IsV4() ? AppendV4String(result) : AppendV6String(result);

    if (appended.HasError())
    {
        return Result<String>(Failure{ appended.Error() });
    }

    return Result<String>(std::move(result));
}

inline Result<String> IpAddress::ToString() const
{
    return ToString(memory::GetDefaultAllocator());
}

//==============================================================================
// Endpoint
//==============================================================================

inline Result<Endpoint> Endpoint::Parse(
    StringView text) noexcept
{
    if (text.Empty())
    {
        return Result<Endpoint>(Failure{ Error(ErrorCode::ParseError) });
    }

    if (text[0] == '[')
    {
        const StringView::SizeType closeBracket = text.Find("]");

        if (closeBracket == StringView::kNotFound ||
            closeBracket + 1 >= text.Size() ||
            text[closeBracket + 1] != ':')
        {
            return Result<Endpoint>(Failure{ Error(ErrorCode::ParseError) });
        }

        const StringView addressText = text.Substr(1, closeBracket - 1);
        const StringView portText = text.Substr(closeBracket + 2);

        Result<IpAddress> address = IpAddress::Parse(addressText);

        if (address.HasError())
        {
            return Result<Endpoint>(Failure{ address.Error() });
        }

        Result<u16> port = detail::ParsePort(portText);

        if (port.HasError())
        {
            return Result<Endpoint>(Failure{ port.Error() });
        }

        return Result<Endpoint>(Endpoint(address.Value(), port.Value()));
    }

    // Plain "host:port" — host must be IPv4 here. An unbracketed IPv6
    // literal is rejected below rather than guessed at, since its own
    // colons would be ambiguous with the port separator.
    const StringView::SizeType lastColon = text.RFind(":");

    if (lastColon == StringView::kNotFound)
    {
        return Result<Endpoint>(Failure{ Error(ErrorCode::ParseError) });
    }

    const StringView addressText = text.Substr(0, lastColon);
    const StringView portText = text.Substr(lastColon + 1);

    Result<IpAddress> address = IpAddress::Parse(addressText);

    if (address.HasError())
    {
        return Result<Endpoint>(Failure{ address.Error() });
    }

    if (address.Value().IsV6())
    {
        return Result<Endpoint>(Failure{ Error(ErrorCode::ParseError) });
    }

    Result<u16> port = detail::ParsePort(portText);

    if (port.HasError())
    {
        return Result<Endpoint>(Failure{ port.Error() });
    }

    return Result<Endpoint>(Endpoint(address.Value(), port.Value()));
}

inline Result<String> Endpoint::ToString(
    memory::Allocator& allocator) const
{
    Result<String> addressText = address_.ToString(allocator);

    if (addressText.HasError())
    {
        return addressText;
    }

    String result(allocator);

    if (address_.IsV6())
    {
        if (Result<void> appended = result.Append("["); appended.HasError())
        {
            return Result<String>(Failure{ appended.Error() });
        }
    }

    if (Result<void> appended = result.Append(addressText.Value().View()); appended.HasError())
    {
        return Result<String>(Failure{ appended.Error() });
    }

    if (address_.IsV6())
    {
        if (Result<void> appended = result.Append("]"); appended.HasError())
        {
            return Result<String>(Failure{ appended.Error() });
        }
    }

    if (Result<void> appended = result.Append(":"); appended.HasError())
    {
        return Result<String>(Failure{ appended.Error() });
    }

    if (Result<void> appended = detail::AppendDecimal(result, port_); appended.HasError())
    {
        return Result<String>(Failure{ appended.Error() });
    }

    return Result<String>(std::move(result));
}

inline Result<String> Endpoint::ToString() const
{
    return ToString(memory::GetDefaultAllocator());
}

} // namespace forge::core
