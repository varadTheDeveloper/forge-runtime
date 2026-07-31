# Forge Core Architecture Specification

## Component

**Core / Socket**

**Status:** Approved (Architecture Frozen)

---

# Purpose

`Socket` is a synchronous, blocking TCP socket — connect out as a
client, or listen/accept as a server, then send/receive bytes. It is
the only forge-core type allowed to call into Winsock, exactly the role
`File` plays for the filesystem (see `File.md`): `IpAddress`/`Endpoint`
(see `IpAddress.md`) describe *where*; `Socket` is *how*.

---

# Responsibilities

* Connecting to a remote `Endpoint` as a client (`Connect`).
* Listening on a local `Endpoint` and accepting incoming connections as
  a server (`Listen`/`Accept`).
* Sending into and receiving from an established connection
  (`Send`/`Receive`).
* Closing (both explicitly and via the destructor — RAII, matching
  every other forge-core resource).
* Translating Winsock failures into `Result<T>`/`Error` — never a raw
  `SOCKET_ERROR`/`WSAGetLastError()` leaking into a caller.
* One-time process-wide Winsock initialization (`WSAStartup`), handled
  internally so callers never have to think about it — same "just
  works" bar as `memory::GetDefaultAllocator()`'s own thread-safe
  lazy-init precedent.

---

# Non-Goals

* Asynchronous I/O. Exactly the same reasoning as `File.md`'s own
  Non-Goals: Forge already has an IOCP-based event loop
  (`platform::IocpLoop`), and `IoCompletion` (see `IocpLoop.h`) is
  explicitly designed to be embeddable by future async socket code —
  but wiring a `Socket` into the loop couples this component to
  whatever the JS-visible networking API ends up looking like, which is
  Runtime Integration's job (a later phase), not this one. This
  `Socket` is the same kind of synchronous, blocking foundation
  `File`/`CreateFile`+`ReadFile`+`WriteFile` are for the filesystem
  side.
* UDP (datagram sockets). TCP covers the overwhelming majority of a
  first networking cut's real use cases (HTTP, most application
  protocols); UDP's very different semantics (no connection, message
  boundaries preserved, no `Listen`/`Accept`) are enough of a distinct
  shape that adding it later as its own type is cleaner than trying to
  make one `Socket` cover both from the start.
* Querying a live socket's local/remote address
  (`getsockname`/`getpeername`), socket options beyond what `Listen`
  needs internally (`SO_REUSEADDR`), or `TCP_NODELAY`/other tuning
  knobs. None of these are needed by anything using `Socket` yet;
  additive later against a real use case.
* DNS resolution — see `IpAddress.md`'s own Non-Goals. `Connect`/
  `Listen` take an already-resolved `Endpoint`.
* Any platform other than Windows. The only backend is Winsock
  (`ws2_32`), matching every other OS-facing forge-core component's
  Win32-only, hard-`#error`-on-anything-else precedent.

---

# Design Goals

* `Result<T>`/`Result<void>` everywhere I/O can fail.
* RAII: a `Socket` that goes out of scope closes its underlying
  `SOCKET`, exactly like `File`/`UniquePtr` release what they own.
* No exceptions.
* Move-only, matching `File` — a `SOCKET` has no well-defined copy
  operation.
* Every Winsock failure is captured through
  `platform::TranslateWinsockError(WSAGetLastError())` (see
  `platform/Win32Error.h` — extended this phase to cover Winsock's own,
  separate error-code space, not just `GetLastError()`'s), mapped onto
  the *existing* generic `ErrorCode` categories wherever one clearly
  fits (`ErrorCode::Timeout` for `WSAETIMEDOUT`, `ErrorCode::AlreadyExists`
  for `WSAEADDRINUSE`, `ErrorCode::PermissionDenied` for `WSAEACCES`)
  and `ErrorCode::PlatformError` otherwise (`NativeCode()` still carries
  the exact WSA error for a caller that needs it) — deliberately **not**
  adding new networking-specific `ErrorCode` values like
  `ConnectionRefused`/`ConnectionReset`, since `Error.md`'s own frozen
  spec explicitly rules out module-specific codes, using
  `SocketDisconnected` as its literal example of what NOT to add.
* `Receive` returning `0` means the peer closed the connection
  gracefully — not an error — mirroring `File::Read`'s own end-of-file-
  is-not-an-error convention exactly.

---

# Public API

```cpp
class Socket
{
public:
    Socket() noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    ~Socket() noexcept;

    [[nodiscard]] static Result<Socket> Connect(const Endpoint& remote);

    // backlog: the OS listen-queue depth (SOMAXCONN-capped internally).
    [[nodiscard]] static Result<Socket> Listen(const Endpoint& local, int backlog = 128);

    // Only valid on a socket created via Listen(). Blocks until a
    // connection arrives.
    [[nodiscard]] Result<Socket> Accept();

    [[nodiscard]] bool IsOpen() const noexcept;

    // Writes up to buffer.Size() bytes; returns the number actually
    // sent (a short send is surfaced as-is, not silently retried into a
    // full send — mirrors File::Write's own policy).
    [[nodiscard]] Result<Size> Send(Span<const u8> buffer);

    // Reads up to buffer.Size() bytes; returns the number actually
    // received — 0 means the peer closed the connection, not an error.
    [[nodiscard]] Result<Size> Receive(Span<u8> buffer);

    void Close() noexcept;

private:
    explicit Socket(void* handle) noexcept;
    void* handle_{ nullptr }; // opaque SOCKET, same reasoning as File's HANDLE
};
```

---

# Memory Layout

A single native `SOCKET` handle stored as an opaque `void*` (`SOCKET`
is `UINT_PTR`, i.e. pointer-sized on every Windows target this project
builds for) — so `<winsock2.h>` never has to be included by `Socket.h`
itself, only `Socket.cpp`, same reasoning as `File.h`'s `HANDLE`-as-
`void*`.

---

# Ownership

`Socket` owns exactly one native socket handle (or none, if default-
constructed or moved-from). Move-only, matching `File`'s ownership
model.

---

# Error Handling Policy

Every method that can fail returns `Result<T>`/`Result<void>`. Never
throws. See Design Goals for the `ErrorCode` mapping policy.

---

# Thread Safety

A single `Socket` instance is not safe to use from multiple threads
concurrently (matches `File`/every other forge-core type — external
synchronization, e.g. a `Mutex` from `Sync.md`, is the caller's job if
needed). Independent `Socket` instances, or a moved-out `Socket`, are
safe to use from different threads — in particular, a common pattern is
one thread blocked in `Accept()` on a listening socket while other
threads (e.g. from a `ThreadPool`, see `Thread.md`) handle already-
accepted connections.

---

# Dependencies

Allowed dependencies:

* Core/Types, Core/Error, Core/Result
* Core/IpAddress (`IpAddress`, `Endpoint`)
* Core/Span
* `platform/Win32Error.h` (`TranslateWinsockError`)
* `<winsock2.h>`/`<ws2tcpip.h>` (`Socket.cpp` only — never `Socket.h`)

Forbidden dependencies:

* `platform/IoLoop`/`IocpLoop` — see Non-Goals re: async. Keeping
  `Socket` IOCP-free now means wiring it into the event loop later is
  additive, not a rewrite (identical reasoning to `File.md`'s own
  Dependencies section).

---

# Extensibility

Future additions may include:

* An async variant built on `IocpLoop`, once a concrete caller (the
  JS-visible networking API, in Runtime Integration) needs one.
* UDP (a separate `DatagramSocket`-style type, not bolted onto this
  one).
* `getsockname`/`getpeername`-backed local/remote address queries,
  `TCP_NODELAY` and other socket options.
* A POSIX backend, if/when Forge targets non-Windows.

Future additions must **not** introduce:

* Exceptions.
* A `<winsock2.h>` include in `Socket.h`.
* New networking-specific `ErrorCode` values (see Design Goals).

---

# Acceptance Criteria

* Public API implemented exactly as specified.
* Every Winsock call's failure path is translated to a `Result`/`Error`,
  never left as a raw `SOCKET_ERROR`/`WSAGetLastError()`.
* **Cannot be compiled or run in this sandbox** — same constraint as
  `File`/`Mutex`/`Thread` before it (no Windows SDK, no working MinGW
  cross-compiler). Verified by careful manual review, plus a
  hand-written mock `<winsock2.h>` compile+link pass under the full
  `-std=c++17 -fno-exceptions -Wall -Wextra -Wpedantic -Werror` bar on
  both compilers. This must be called out explicitly wherever `Socket`
  is described as "done" — implemented, not confirmed, until a real
  `mach build` (or a standalone Visual Studio smoke test, matching
  `ThreadingSmokeTest.cpp`'s/`FileSmokeTest.cpp`'s precedent) confirms
  it on the actual machine.

---

# Implementation Status

* Architecture: Approved
* Public API: Frozen
* Header: Complete (`Socket.h`)
* Implementation: Complete (`Socket.cpp`) — **NOT yet real-build-confirmed**.
  Cannot be compiled or run in the sandbox this was written in (no
  Windows SDK, no working MinGW cross-compiler). Verified by careful
  manual review, plus a hand-written mock `<winsock2.h>`/`<ws2tcpip.h>`
  compile+link pass (both standalone and linked with a driver exercising
  every public entry point) clean under `g++`/`clang++ -std=c++17
  -fno-exceptions -Wall -Wextra -Wpedantic -Werror` (+
  `-Wc++20-extensions` on clang) and ASan+UBSan. This must be called out
  explicitly wherever `Socket` is described as "done" until a real
  `mach build` (or a standalone Visual Studio run of
  `SocketSmokeTest.cpp`) confirms it on the actual machine.
* Tests: `SocketSmokeTest.cpp` written — a real loopback client/server
  test: listens on a fixed high port (`127.0.0.1:53421`; `127.0.0.1:0`-
  style ephemeral-port binding is not supported by this first cut's
  fixed-port `Endpoint::Parse` input), accepts on a background `Thread`,
  echoes back whatever it receives, and the main thread (as client)
  verifies the echoed bytes match byte-for-byte, plus a second test that
  connecting to a port nothing is listening on fails cleanly. Syntax/
  link-checked against the mock (compiles and links clean on both
  compilers) but **not run** — like `FileSmokeTest.cpp`/
  `ThreadingSmokeTest.cpp`, running it against the mock would just
  exercise dumb stubs, not a real TCP stack, so there's nothing
  meaningful to learn from executing it here. Runnable only on the
  user's Windows machine.
