# Forge Core Architecture Specification

## Component

**Core / File**

**Status:** Approved (Architecture Frozen)

---

# Purpose

`File` performs actual filesystem I/O — opening, reading, writing, and
closing a file, plus the small set of whole-filesystem queries (exists,
create directory, remove, directory listing) a first pass needs. `Path`
(see `Path.md`) describes *where*; `File` is the only forge-core type
that is allowed to call into the OS to act on that location.

---

# Responsibilities

The `File` component is responsible for:

* Opening a file with an explicit, small set of modes (read / write /
  read-write / append / create-if-missing / truncate).
* Reading into a caller-provided buffer and writing from one.
* Seeking and reporting position/size.
* Closing (both explicitly and via the destructor — RAII, same as every
  other forge-core resource).
* A handful of static, path-only queries/operations that don't need an
  open handle: `Exists`, `MakeDirectory` (and `CreateDirectories` for
  the recursive case), `Remove`, `ReadAllBytes`/`ReadAllText`
  convenience wrappers. Named `MakeDirectory` rather than
  `CreateDirectory` because `<windows.h>` `#define`s `CreateDirectory`
  to `CreateDirectoryA`/`CreateDirectoryW` — a real `mach build` caught
  this colliding with our own method name (see `HISTORY.md`).
* Translating Win32 failures into `Result<T>`/`Error` — never a raw
  `BOOL`/`GetLastError()` leaking into a caller.

---

# Non-Goals

`File` is **not** responsible for:

* Asynchronous I/O. Forge already has an IOCP-based event loop
  (`forge-core/platform/IocpLoop`), and file I/O *can* be done through
  IOCP too — but wiring that up couples this component to the event
  loop and to whatever the JS-visible `fs` API ends up looking like,
  which is `forge.cpp`/Runtime-Integration territory (a later phase),
  not this one. `File` here is a synchronous, blocking primitive —
  the same role `open`/`read`/`write`/`close` (or `CreateFile`/
  `ReadFile`/`WriteFile`/`CloseHandle`) play as the foundation
  underneath both a sync and an eventual async API. Revisit once
  Runtime Integration needs an async `fs` surface.
* Memory-mapped files, file locking, watching/notifications, symlink
  creation, permissions/ACL manipulation — all plausible future
  additions, none needed by anything using `File` yet.
* Any platform other than Windows. The only backend implemented is
  Win32 (`CreateFileW`/`ReadFile`/`WriteFile`/`DeleteFileW`/
  `CreateDirectoryW`/`FindFirstFileW`), matching `IocpLoop`'s own
  precedent of a hard `#error` on anything else rather than a
  half-working POSIX shim nobody has verified.
* Text encoding conversion beyond UTF-8 (`Path`/`File` internally use
  UTF-8 and convert to UTF-16 only at the Win32 call boundary, via
  `MultiByteToWideChar`/`WideCharToMultiByte` — never exposed to
  callers).

---

# Design Goals

* `Result<T>`/`Result<void>` everywhere I/O can fail — which is
  everywhere, since I/O touches the OS.
* RAII: a `File` that goes out of scope closes its handle, exactly like
  `UniquePtr` releases its pointee. No explicit `Close()` call required
  for correctness (though one is provided, same as `Vector::Clear()`
  exists even though the destructor would also do it).
* No exceptions.
* Every Win32 failure is captured as `Error(ErrorCode::PlatformError,
  static_cast<i32>(GetLastError()))` unless a more specific `ErrorCode`
  clearly applies (`ErrorCode::NotFound` for
  `ERROR_FILE_NOT_FOUND`/`ERROR_PATH_NOT_FOUND`,
  `ErrorCode::PermissionDenied` for `ERROR_ACCESS_DENIED`,
  `ErrorCode::AlreadyExists` for `ERROR_ALREADY_EXISTS` on a
  create-exclusive open) — callers that only care "did this fail" can
  check `Result<T>::HasError()`; callers that need the exact Win32 code
  can read `NativeCode()` off the `Error`.

---

# Public API

## FileMode

```
enum class FileMode : u8
{
    Read,          // must exist; read-only
    Write,         // create or truncate; write-only
    ReadWrite,     // create or truncate; read+write
    Append,        // create if missing; write-only; all writes seek to end first
    CreateNew,     // fails with ErrorCode::AlreadyExists if the file exists
};
```

## File

```
class File
{
public:
    File() noexcept;                 // no handle owned
    File(const File&) = delete;      // a Win32 HANDLE has no cheap copy —
    File& operator=(const File&) = delete; // same non-copyable precedent
                                            // as would apply to any single-
                                            // owner OS resource; matches
                                            // UniquePtr's own move-only
                                            // shape rather than Vector's
                                            // copyable-value shape.
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    ~File() noexcept;

    [[nodiscard]] static Result<File> Open(const Path& path, FileMode mode);

    [[nodiscard]] bool IsOpen() const noexcept;

    // Reads up to `buffer.Size()` bytes; returns the number actually
    // read (0 at end-of-file, which is not itself an error).
    [[nodiscard]] Result<Size> Read(Span<u8> buffer);

    // Writes `buffer.Size()` bytes; returns the number actually written
    // (short writes are surfaced, not silently retried into a full
    // write, mirroring what WriteFile itself reports).
    [[nodiscard]] Result<Size> Write(Span<const u8> buffer);

    enum class SeekOrigin : u8 { Begin, Current, End };
    [[nodiscard]] Result<u64> Seek(i64 offset, SeekOrigin origin);
    [[nodiscard]] Result<u64> Tell();
    [[nodiscard]] Result<u64> SizeInBytes();

    void Close() noexcept;

    // Path-only static operations — no open handle required.
    [[nodiscard]] static Result<bool> Exists(const Path& path);
    [[nodiscard]] static Result<void> MakeDirectory(const Path& path);
    [[nodiscard]] static Result<void> CreateDirectories(const Path& path); // recursive, like `mkdir -p`
    [[nodiscard]] static Result<void> Remove(const Path& path);

    // Convenience wrappers, implemented in terms of Open+Read+Close.
    [[nodiscard]] static Result<Vector<u8>> ReadAllBytes(const Path& path);
    [[nodiscard]] static Result<String> ReadAllText(const Path& path);

private:
    void* handle_{ nullptr }; // HANDLE, kept as void* so this header
                              // never has to #include <windows.h> itself
                              // (only File.cpp does) — same reasoning as
                              // IocpLoop keeping its OVERLAPPED-derived
                              // types out of its own public header where
                              // it can.
};
```

---

# Memory Layout

A single native handle (`void*`, actually a Win32 `HANDLE` — opaque here
so `<windows.h>` doesn't leak into every translation unit that includes
`File.h`). No buffering, no internal `Vector`/`String` state.

---

# Ownership

`File` owns exactly one OS handle (or none, if default-constructed or
moved-from). Move-only, matching `UniquePtr<T>`'s ownership model, not
`Vector<T>`'s value-copy model — a Win32 `HANDLE` has no well-defined
"duplicate this file's read/write cursor and buffering state" copy
operation cheap enough to make copyable-by-default a good default,
unlike `Vector`'s heap buffer (which genuinely can be deep-copied
cheaply relative to what it holds).

---

# Error Handling Policy

Every method that can fail returns `Result<T>`/`Result<void>`. Never
throws — exceptions are disabled in the real build regardless.
`ReadAllBytes`/`ReadAllText` surface both the `Open` failure and any
subsequent `Read` failure through the same `Result<T>` return, without
distinguishing which stage failed in the type system (the `Error`
itself, via `Code()`/`NativeCode()`, still carries that information).

---

# Thread Safety

A single `File` instance is not safe to use from multiple threads
concurrently (matches `Vector`/`HashMap`/every other forge-core type —
external synchronization is the caller's job). Independent `File`
instances on different paths, or a moved-out `File`, are safe to use
from different threads.

---

# Dependencies

Allowed dependencies:

* Core/Types, Core/Error, Core/Result
* Core/Path
* Core/Span, Core/Vector, Core/String (for the buffer-based Read/Write
  and the ReadAllBytes/ReadAllText convenience wrappers)
* `<windows.h>` (File.cpp only — never File.h)

Forbidden dependencies:

* `forge-core/platform/IoLoop`/`IocpLoop` — see Non-Goals re: async.
  Keeping `File` IOCP-free now means wiring it into the event loop
  later is additive, not a rewrite.

---

# Extensibility

Future additions may include:

* An async variant built on `IocpLoop`, once a concrete caller (the
  JS-visible `fs` API, in Runtime Integration) needs one — likely as a
  separate type/free functions rather than retrofitting async behavior
  onto this synchronous one.
* A POSIX backend, if/when Forge targets non-Windows — behind the same
  `#if defined(_WIN32)` / hard `#error` pattern `IoLoop.h` already
  establishes.
* Memory-mapped file support, file watching, permissions.

Future additions must **not** introduce:

* Exceptions.
* A `<windows.h>` include in `File.h` (only `File.cpp`).

---

# Acceptance Criteria

* Public API implemented exactly as specified.
* Every Win32 call's failure path is translated to a `Result`/`Error`,
  never left as a raw `BOOL`/`GetLastError()`.
* **Cannot be compiled or run in this sandbox** — there is no Windows
  SDK / `<windows.h>` available here, and no MinGW cross-compiler could
  be installed (network access here is allowlisted to package
  registries, not the Ubuntu `universe` component MinGW ships in). This
  component is verified by careful manual review only until a real
  `mach build` (or a standalone Visual Studio smoke test, matching
  `IocpSmokeTest.cpp`'s precedent from Phase 2 of the event-loop work)
  confirms it on the actual machine. This must be called out explicitly
  wherever `File` is described as "done" — it is implemented, not
  confirmed, until that happens.

---

# Implementation Status

* Architecture: Approved
* Public API: Frozen
* Header: Pending
* Implementation: Pending
* Tests: Pending (design: a real-filesystem test — create a temp file,
  write/read/seek/verify contents, clean up — runnable only on the
  user's Windows machine, same constraint as the rest of this
  component)
