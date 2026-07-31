#include "Win32Error.h"

// TranslateWinsockError below needs the WSAE*-prefixed constants, which
// live in <winsock2.h>, not <windows.h>. <windows.h> pulls in the legacy
// <winsock.h> by default unless WIN32_LEAN_AND_MEAN is defined first,
// and <winsock.h>/<winsock2.h> included together (in the wrong order)
// is a classic redefinition-error trap — so WIN32_LEAN_AND_MEAN is
// defined here and <winsock2.h> is included before <windows.h>,
// matching the include order Socket.cpp also has to follow.
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

namespace forge::core::platform
{

Error TranslateWin32Error(
    unsigned long lastError) noexcept
{
    switch (lastError)
    {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return Error(ErrorCode::NotFound, static_cast<i32>(lastError));

        case ERROR_ACCESS_DENIED:
            return Error(ErrorCode::PermissionDenied, static_cast<i32>(lastError));

        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS:
            return Error(ErrorCode::AlreadyExists, static_cast<i32>(lastError));

        // The GetLastError() code SleepConditionVariableSRW/CS leaves set
        // when the wait times out (see ConditionVariable::WaitFor) —
        // distinct from WAIT_TIMEOUT, which is a WaitForSingleObject
        // *return value*, not a GetLastError() code, so it never reaches
        // this function.
        case ERROR_TIMEOUT:
            return Error(ErrorCode::Timeout, static_cast<i32>(lastError));

        default:
            return Error(ErrorCode::PlatformError, static_cast<i32>(lastError));
    }
}

Error TranslateWinsockError(
    int wsaError) noexcept
{
    switch (wsaError)
    {
        // The peer didn't respond in time (e.g. a connect() attempt to an
        // unreachable/filtered address) — maps onto the same generic
        // Timeout category ERROR_TIMEOUT does for Win32, even though the
        // two constants are numerically unrelated.
        case WSAETIMEDOUT:
            return Error(ErrorCode::Timeout, static_cast<i32>(wsaError));

        // bind() to an address already in use by another listener.
        case WSAEADDRINUSE:
            return Error(ErrorCode::AlreadyExists, static_cast<i32>(wsaError));

        // A firewall/socket-permission policy blocked the operation.
        case WSAEACCES:
            return Error(ErrorCode::PermissionDenied, static_cast<i32>(wsaError));

        // Deliberately no cases for connection-specific failures such as
        // WSAECONNREFUSED/WSAECONNRESET/WSAENETUNREACH: Error.md's frozen
        // spec explicitly excludes module-specific ErrorCode values like
        // SocketDisconnected, so these fall through to PlatformError below,
        // with NativeCode() preserving the exact WSA error for a caller
        // that needs to distinguish them.
        default:
            return Error(ErrorCode::PlatformError, static_cast<i32>(wsaError));
    }
}

} // namespace forge::core::platform
