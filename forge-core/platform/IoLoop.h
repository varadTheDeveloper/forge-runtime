#pragma once

// Compile-time backend selection for the event loop, per ROADMAP.md Phase 2.
//
// A type alias rather than a virtual interface: the loop is on the hottest
// path in the whole runtime (every timer tick and every I/O completion goes
// through it), so this avoids a vtable indirection per call, and avoids
// needing a converting/upcasting constructor on the frozen UniquePtr<T> just
// to hold a polymorphic loop by pointer. Every backend must expose the same
// member surface (see IocpLoop.h for the canonical shape) so call sites never
// need to change when a Linux backend (io_uring or epoll) is added later.

#if defined(_WIN32)

#include "IocpLoop.h"

namespace forge::core::platform
{

using IoLoop = IocpLoop;

} // namespace forge::core::platform

#else

#error "forge::core::platform::IoLoop: no backend implemented for this platform yet. " \
       "Phase 2 (see ROADMAP.md) only covers Windows/IOCP so far; a Linux " \
       "(io_uring/epoll) or macOS (kqueue) backend needs to be added here " \
       "before building on non-Windows."

#endif
