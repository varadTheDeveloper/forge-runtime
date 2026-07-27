# Forge Core Architecture Specification

## Component

**Core / Error**

**Status:** Approved (Architecture Frozen)

---

# Purpose

`Error` represents a lightweight, platform-independent, recoverable error.

It is the canonical error value used throughout Forge Core and is the failure type stored by `Result<T>`.

Its purpose is to communicate *what* failed without performing logging, formatting, exception handling, or recovery.

---

# Responsibilities

The `Error` component is responsible for:

* Representing recoverable failures.
* Providing a canonical `ErrorCode`.
* Preserving the underlying native platform error code when available.
* Remaining lightweight enough to be stored inline inside `Result<T>`.

---

# Non-Goals

`Error` is **not** responsible for:

* Throwing exceptions.
* Logging.
* Formatting error messages.
* Localization.
* Stack traces.
* Source locations.
* Error recovery.
* Platform-specific APIs.
* Module-specific error types.

---

# Design Goals

* Small value type.
* No dynamic memory allocation.
* No ownership of external resources.
* Platform independent.
* Trivially movable.
* Cheap to copy.
* Suitable for `constexpr` usage where possible.
* Suitable for inline storage inside `Result<T>`.

---

# Public API

## ErrorCode

Forge Core provides a single canonical error enumeration.

The enumeration contains only generic error concepts.

Module-specific values such as `FileNotFound` or `SocketDisconnected` are intentionally excluded.

Example categories include:

* None
* Unknown
* InvalidArgument
* InvalidOperation
* NotSupported
* NotImplemented
* NotFound
* AlreadyExists
* PermissionDenied
* Busy
* Timeout
* Cancelled
* EndOfFile
* IOError
* OutOfMemory
* BufferTooSmall
* Overflow
* Underflow
* InvalidData
* ParseError
* PlatformError

---

## NativeError

Forge Core defines a platform-independent alias representing an operating system error identifier.

Current implementation:

* `using NativeError = i32;`

Future platforms may redefine this alias without affecting the public API.

---

## Error

The `Error` class stores only:

* ErrorCode
* NativeError

No additional state is stored.

---

# Memory Layout

Expected members:

* ErrorCode
* NativeError

Target characteristics:

* No heap allocation
* Trivially movable
* Small enough for efficient pass-by-value
* Suitable for inline storage within `Result<T>`

---

# Ownership

`Error` owns no external resources.

It manages no heap memory.

It performs no reference counting.

It is a pure value type.

---

# Error Handling Policy

`Error` never throws exceptions.

Construction, copying, moving, and destruction are expected to be `noexcept`.

---

# Thread Safety

`Error` is immutable after construction.

Independent instances may be safely copied and moved between threads.

---

# Dependencies

Allowed dependencies:

* Core/Types
* Standard Library headers required for primitive definitions

Forbidden dependencies:

* Filesystem
* Memory
* Threading
* Networking
* Process
* Runtime
* Platform-specific headers

---

# Relationship with Result<T>

`Result<T>` embeds an `Error` directly.

`Error` must therefore remain:

* lightweight
* allocation-free
* inexpensive to copy and move

`Error` is intentionally independent of `Result<T>` even though it is primarily consumed by it.

---

# Extensibility

Future additions may include:

* Additional generic `ErrorCode` values.
* Platform-specific conversion helpers implemented outside the core value type.

Future additions must **not** introduce:

* Dynamic allocation.
* Logging functionality.
* Formatting responsibilities.
* Module-specific error codes.
* Platform-specific public types.

---

# Acceptance Criteria

The component is considered complete when:

* Public API is implemented exactly as specified.
* Unit tests pass.
* Compile-time assertions validate expected type properties.
* No unnecessary dependencies are introduced.
* The API remains stable unless a significant architectural issue is discovered.

---

# Implementation Status

* Architecture: Approved
* Public API: Frozen
* Header: Pending
* Implementation: Pending
* Tests: Pending
