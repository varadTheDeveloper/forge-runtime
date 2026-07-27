#pragma once

#include "ResultFwd.h"
#include "../Error.h"
#include "../Failure.h"

namespace forge::core
{

template<>
class [[nodiscard]] Result<void>
{
public:
    //==========================================================================
    // Construction
    //==========================================================================

    Result();

    explicit Result(const Failure& failure);

    explicit Result(Failure&& failure);

    Result(const Result& other);

    Result(Result&& other) noexcept;

    ~Result();

    //==========================================================================
    // Assignment
    //==========================================================================

    Result& operator=(const Result& other);

    Result& operator=(Result&& other) noexcept;

    //==========================================================================
    // State
    //==========================================================================

    [[nodiscard]]
    bool HasValue() const noexcept;

    [[nodiscard]]
    bool HasError() const noexcept;

    [[nodiscard]]
    explicit operator bool() const noexcept;

    //==========================================================================
    // Utilities
    //==========================================================================

    void Ignore() const noexcept;

    //==========================================================================
    // Error Access
    //==========================================================================

    [[nodiscard]]
    class Error& Error() noexcept;

    [[nodiscard]]
    const class Error& Error() const noexcept;

private:

    forge::core::Error error_;

    bool has_value_{ true };
};

} // namespace forge::core

#include "ResultVoid.inl"