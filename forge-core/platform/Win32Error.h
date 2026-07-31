#pragma once

// Windows-only, matching every other OS-facing component in forge-core
// (IoLoop/IocpLoop, File, Mutex/ConditionVariable, Thread/ThreadPool) —
// no non-Windows backend exists anywhere in this codebase yet.
#if !defined(_WIN32)
#error "forge::core::platform::TranslateWin32Error: Windows-only."
#endif

#include "../Error.h"

namespace forge::core::platform
{

/// Translates a Win32 GetLastError() code into a forge::core::Error.
/// Extracted out of File.cpp (which originally had its own private copy
/// of this exact switch) so File, Mutex/ConditionVariable, and
/// Thread/ThreadPool all share one ERROR_* -> ErrorCode mapping instead
/// of three copies drifting independently — see HISTORY.md's Phase 4
/// entry.
///
/// Takes `unsigned long` rather than `DWORD` so this header never has
/// to #include <windows.h> itself (DWORD is unsigned long on every
/// Windows target this project builds for; Win32Error.cpp, which does
/// include <windows.h>, passes GetLastError()'s DWORD result straight
/// through with no cast needed).
[[nodiscard]]
Error TranslateWin32Error(
    unsigned long lastError) noexcept;

/// Translates a Winsock WSAGetLastError() code into a forge::core::Error.
/// A distinct function from TranslateWin32Error rather than an overload,
/// because Winsock errors live in their own numbering space (WSAGetLastError()
/// returns a plain `int`, not a `DWORD`, and its WSAE*-prefixed values are
/// disjoint from GetLastError()'s ERROR_*-prefixed ones) — collapsing them
/// into one overload set would invite exactly the kind of silent mix-up
/// this split exists to prevent.
///
/// Used by Socket.cpp (see Socket.md). Deliberately maps onto the
/// *existing* generic ErrorCode categories only (Timeout/AlreadyExists/
/// PermissionDenied/PlatformError) rather than introducing new
/// networking-specific ErrorCode values — Error.md's frozen spec
/// explicitly rules those out, using SocketDisconnected as its own
/// example of what NOT to add. NativeCode() on the returned Error still
/// carries the exact WSA error number for a caller that needs it.
[[nodiscard]]
Error TranslateWinsockError(
    int wsaError) noexcept;

} // namespace forge::core::platform
