#pragma once

#include <cassert>

///============================================================================
/// Forge Assertions
///
/// Assertions are intended to catch programmer errors during development.
/// They are compiled out when NDEBUG is defined.
///============================================================================

#ifndef FORGE_ASSERT
    #define FORGE_ASSERT(condition) assert(condition)
#endif