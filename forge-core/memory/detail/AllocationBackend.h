#pragma once

#include "../../Types.h"

namespace forge::core::memory::detail
{

[[nodiscard]]
void* Allocate(
    Size size,
    Size alignment) noexcept;

void Deallocate(
    void* memory,
    Size alignment) noexcept;

} // namespace forge::core::memory::detail