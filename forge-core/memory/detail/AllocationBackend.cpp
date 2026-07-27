#include "AllocationBackend.h"

#include <new>

namespace forge::core::memory::detail
{

void* Allocate(
    Size  size,
    Size alignment) noexcept
{
    if (size == 0)
    {
        return nullptr;
    }

    // Power-of-two check, equivalent to C++20's std::has_single_bit
    // (<bit>), which this codebase cannot rely on — Gecko's own build of
    // it does not compile in full C++20 mode (see HISTORY.md). A power of
    // two has exactly one bit set, so ANDing it with itself minus one
    // clears that bit, leaving zero only for powers of two (zero itself
    // is already excluded by the check above).
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
    {
        return nullptr;
    }

    // The non-throwing overload, not try/catch around the throwing one:
    // exceptions are disabled entirely in Gecko's build of this codebase
    // (a hard compile error, not just a style preference — see
    // HISTORY.md), and this codebase's own zero-exception design
    // (AGENTS.md) forbids relying on them regardless of build config.
    return ::operator new(
        size,
        std::align_val_t{alignment},
        std::nothrow);
}

void Deallocate(
    void* memory,
    Size  alignment) noexcept
{
    ::operator delete(
        memory,
        std::align_val_t{alignment});
}

} // namespace forge::core::memory::detail