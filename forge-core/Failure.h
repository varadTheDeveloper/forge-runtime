#pragma once

#include <utility>

#include "Error.h"

namespace forge::core
{

/// Wrapper type used to explicitly construct a failed Result.
///
/// Prevents ambiguity between:
///
///     Result<Error> value{ Error{...} };
///
/// and
///
///     Result<Error> failure{ Failure{ Error{...} } };
///
class [[nodiscard]] Failure
{
public:

    constexpr explicit Failure(
        const class Error& error) noexcept
        :
        m_error(error)
    {
    }

    constexpr explicit Failure(
        class Error&& error) noexcept
        :
        m_error(std::move(error))
    {
    }

    [[nodiscard]]
    constexpr const class Error& Error() const noexcept
    {
        return m_error;
    }

private:

    forge::core::Error m_error;
};

} // namespace forge::core