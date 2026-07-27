#pragma once

#include <utility>

#include "Error.h"

namespace forge::core
{

template<typename T>
union ResultStorage
{
    // Not constexpr: a constexpr destructor on a union with a non-trivial
    // member is a C++20 feature (this codebase's real build target is
    // C++17 — see HISTORY.md), and nothing here is ever constructed in a
    // constant-evaluated context anyway, so there's no functional loss.
    ResultStorage() noexcept
    {
    }

    ~ResultStorage() noexcept
    {
    }

    T value;
    Error error;
};

} // namespace forge::core