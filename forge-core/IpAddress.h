#pragma once

#include "Error.h"
#include "Result.h"
#include "Span.h"
#include "String.h"
#include "StringView.h"
#include "Types.h"
#include "memory/Allocator.h"
#include "memory/DefaultAllocator.h"
#include "memory/ResultVoid.h"

namespace forge::core
{

enum class IpVersion : u8
{
    V4,
    V6,
};

/// A parsed, validated IPv4 or IPv6 address. Pure value type — no OS
/// dependency, no allocation, constructors never fail. See IpAddress.md
/// for the full spec. `Socket` (see Socket.md) is the OS-facing
/// counterpart that actually uses one of these to connect/bind/etc. —
/// same `Path`/`File` split established in Phase 3, applied to
/// networking.
class [[nodiscard]] IpAddress
{
public:

    using SizeType = forge::core::Size;

    /// Default: the IPv4 unspecified address, 0.0.0.0.
    IpAddress() noexcept = default;

    [[nodiscard]]
    static IpAddress V4(
        u8 a,
        u8 b,
        u8 c,
        u8 d) noexcept;

    /// `bytes` must be exactly 16 bytes — a caller passing the wrong
    /// size is a programming error, not a runtime failure mode
    /// (asserted, matching the "constructors never fail" precedent
    /// fixed-size types like Array<T, N> already follow in this
    /// codebase).
    [[nodiscard]]
    static IpAddress V6(
        Span<const u8> bytes) noexcept;

    /// Accepts numeric IPv4 dotted-quad or IPv6 colon-hex text only —
    /// no hostnames, see IpAddress.md's Non-Goals. Rejects an IPv4
    /// octet with a leading zero (e.g. "010.0.0.1") rather than
    /// guessing whether it means octal or decimal — see IpAddress.md.
    [[nodiscard]]
    static Result<IpAddress> Parse(
        StringView text) noexcept;

    [[nodiscard]]
    IpVersion Version() const noexcept
    {
        return version_;
    }

    [[nodiscard]]
    bool IsV4() const noexcept
    {
        return version_ == IpVersion::V4;
    }

    [[nodiscard]]
    bool IsV6() const noexcept
    {
        return version_ == IpVersion::V6;
    }

    /// 4 bytes for a V4 address, 16 bytes for V6 — the exact bytes a
    /// caller building a sockaddr_in/sockaddr_in6 (see Socket.cpp) needs,
    /// with no text round-trip required on that path.
    [[nodiscard]]
    Span<const u8> Bytes() const noexcept;

    [[nodiscard]]
    Result<String> ToString(
        memory::Allocator& allocator) const;

    [[nodiscard]]
    Result<String> ToString() const;

    // Defined inline (not deferred to IpAddress.inl) — a [[nodiscard]]
    // friend declaration without a body is rejected by both GCC and
    // Clang ("attribute ignored"/"an attribute list cannot appear
    // here"), so this has to be a defining friend declaration right
    // here, matching Path.h's/Error.h's own established pattern for
    // frozen comparison operators.
    [[nodiscard]]
    friend bool operator==(
        const IpAddress& lhs,
        const IpAddress& rhs) noexcept
    {
        if (lhs.version_ != rhs.version_)
        {
            return false;
        }

        const Size count = lhs.IsV4() ? 4 : 16;

        for (Size i = 0; i < count; ++i)
        {
            if (lhs.bytes_[i] != rhs.bytes_[i])
            {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]]
    friend bool operator!=(
        const IpAddress& lhs,
        const IpAddress& rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:

    [[nodiscard]]
    static Result<IpAddress> ParseV4(
        StringView text) noexcept;

    [[nodiscard]]
    static Result<IpAddress> ParseV6(
        StringView text) noexcept;

    Result<void> AppendV4String(
        String& out) const;

    Result<void> AppendV6String(
        String& out) const;

    IpVersion version_{ IpVersion::V4 };
    u8 bytes_[16]{};
};

/// An IpAddress plus a port number. See IpAddress.md.
class [[nodiscard]] Endpoint
{
public:

    Endpoint() noexcept = default;

    Endpoint(
        IpAddress address,
        u16 port) noexcept
        :
        address_(address),
        port_(port)
    {
    }

    /// Accepts "host:port" for an IPv4 host, or the bracketed
    /// "[host]:port" form for an IPv6 host — the brackets are required
    /// for V6 (an unbracketed IPv6 literal's own colons would be
    /// ambiguous with the port separator), matching standard networking
    /// convention (e.g. URLs). Passing an unbracketed V6 address is
    /// rejected rather than guessed at.
    [[nodiscard]]
    static Result<Endpoint> Parse(
        StringView text) noexcept;

    [[nodiscard]]
    const IpAddress& Address() const noexcept
    {
        return address_;
    }

    [[nodiscard]]
    u16 Port() const noexcept
    {
        return port_;
    }

    [[nodiscard]]
    Result<String> ToString(
        memory::Allocator& allocator) const;

    [[nodiscard]]
    Result<String> ToString() const;

    [[nodiscard]]
    friend bool operator==(
        const Endpoint& lhs,
        const Endpoint& rhs) noexcept
    {
        return lhs.address_ == rhs.address_ && lhs.port_ == rhs.port_;
    }

    [[nodiscard]]
    friend bool operator!=(
        const Endpoint& lhs,
        const Endpoint& rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:

    IpAddress address_;
    u16 port_{ 0 };
};

} // namespace forge::core

#include "IpAddress.inl"
