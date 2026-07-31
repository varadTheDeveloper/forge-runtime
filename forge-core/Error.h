#pragma once

#include "Types.h"

namespace forge::core
{

/// Canonical recoverable error codes used throughout Forge Core.
enum class ErrorCode : u32
{
    None = 0,

    Unknown,

    InvalidArgument,
    InvalidOperation,

    NotSupported,
    NotImplemented,

    NotFound,
    AlreadyExists,

    PermissionDenied,

    Busy,
    Timeout,
    Cancelled,

    EndOfFile,

    IOError,

    OutOfMemory,

    BufferTooSmall,

    Overflow,
    Underflow,

    InvalidData,

    ParseError,

    /// A failure whose only useful detail is the raw native/OS error code
    /// carried in NativeCode() — the caller should inspect NativeCode()
    /// rather than branch on Code() alone. Listed in Error.md's frozen
    /// spec (see "Public API" -> "ErrorCode") but missing from this
    /// enum until Phase 3 needed it for Win32 file I/O failures that
    /// don't map cleanly onto any of the categories above.
    PlatformError
};

/// Lightweight value type representing a recoverable error.
class [[nodiscard]] Error
{
public:

    constexpr Error() noexcept = default;

    explicit constexpr Error(
        ErrorCode code,
        i32 nativeCode = 0) noexcept
        :
        m_code(code),
        m_nativeCode(nativeCode)
    {
    }

    [[nodiscard]]
    constexpr ErrorCode Code() const noexcept
    {
        return m_code;
    }

    [[nodiscard]]
    constexpr i32 NativeCode() const noexcept
    {
        return m_nativeCode;
    }

    explicit constexpr operator bool() const noexcept
    {
        return m_code != ErrorCode::None;
    }
/// @brief Compares two Error objects.
/// @param lhs The left-hand Error.
/// @param rhs The right-hand Error.
/// @return True if both Error objects are equal.
    // Written out explicitly rather than `= default`: defaulting a
    // non-member/friend comparison operator like this is a C++20 feature,
    // and this codebase's real build target is C++17 (see HISTORY.md).
    // Same comparison semantics either way (member-wise equality).
    friend constexpr bool operator==(
        const Error& lhs,
        const Error& rhs) noexcept
    {
        return lhs.m_code == rhs.m_code && lhs.m_nativeCode == rhs.m_nativeCode;
    }

    friend constexpr bool operator==(
        const Error& error,
        ErrorCode code) noexcept
    {
        return error.m_code == code;
    }

    friend constexpr bool operator==(
        ErrorCode code,
        const Error& error) noexcept
    {
        return error == code;
    }

private:

    ErrorCode m_code{ ErrorCode::None };
    i32       m_nativeCode{ 0 };
};

} // namespace forge::core