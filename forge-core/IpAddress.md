# Forge Core Architecture Specification

## Component

**Core / IpAddress (IpAddress, Endpoint)**

**Status:** Approved (Architecture Frozen)

---

# Purpose

`IpAddress` is a parsed, validated IPv4 or IPv6 address value — pure
data, no OS dependency. `Endpoint` pairs one with a port number. Both
exist so `Socket` (see `Socket.md`) never has to parse or format
addresses itself — the same split `Path`/`File` established in Phase 3
(one type never touches the OS; the other is entirely the OS), applied
here to networking.

---

# Responsibilities

* Parse IPv4 dotted-quad text (`"192.168.1.1"`) and IPv6 colon-hex text
  (`"2001:db8::1"`, including `::` zero-run compression) into a
  validated, fixed-size byte representation.
* Format an `IpAddress` back to canonical text — for IPv6, using RFC
  5952's canonical form (lowercase hex, the longest run of zero groups
  compressed with a single `::`).
* `Endpoint::Parse` accepts `"host:port"` for IPv4 and the standard
  bracketed `"[host]:port"` for IPv6 (required for IPv6, since an
  unbracketed address's own colons would be ambiguous with the port
  separator).

---

# Non-Goals

* DNS resolution / hostname lookup. `IpAddress::Parse` only accepts
  already-numeric text — `"localhost"` or `"example.com"` fail to parse
  with `ErrorCode::ParseError`, the same as any other malformed input.
  A resolver is plausible future work (see Extensibility) but is a
  fundamentally different kind of operation (it can block, hit the
  network, and fail for reasons unrelated to text syntax) and doesn't
  belong in a pure, allocation-free value type.
* IPv4-mapped IPv6 textual notation (`"::ffff:192.0.2.1"`) and IPv6 zone
  IDs (`"fe80::1%eth0"`). Both are real, valid IPv6 syntax, but adding
  them means deciding how they interact with equality/formatting/
  `Socket`'s `sockaddr_in6` construction — deferred until something
  concretely needs either, rather than guessing now.
* Any parsing leniency that could be a security foot-gun. In
  particular, an IPv4 octet with a leading zero (`"010.0.0.1"`) is
  **rejected**, not interpreted as octal — this is a well-known
  historical ambiguity (some C library `inet_aton` implementations
  read a leading-zero octet as octal, others as decimal, which has
  caused real SSRF/access-control bypass bugs when a validator and a
  connector disagreed on which). Being strict here means Forge never
  has that class of bug: leading zeros are simply invalid.

---

# Design Goals

* No allocation on the parse or comparison path. `IpAddress` stores a
  fixed 16-byte buffer (used fully for V6, first 4 bytes for V4) plus a
  version tag — the same "plain fixed-size data, no invariant beyond
  its own bytes" reasoning `Array<T, N>` already uses in this codebase.
  Only formatting to text (`ToString`) allocates, since it produces an
  owned `String`.
* `Result<IpAddress>`/`Result<Endpoint>` for `Parse`, with
  `ErrorCode::ParseError` for any malformed input — reuses the existing
  generic error category rather than inventing a networking-specific
  one (`Error.md`'s own frozen spec explicitly excludes module-specific
  codes like `SocketDisconnected`; the same reasoning rules out a
  hypothetical `InvalidAddress`).
* Byte-for-byte, allocation-free `Bytes()` accessor so `Socket.cpp` can
  copy directly into a `sockaddr_in`/`sockaddr_in6` without going
  through text at all on the hot (connect/accept) path.

---

# Public API

```cpp
enum class IpVersion : u8 { V4, V6 };

class IpAddress
{
public:
    constexpr IpAddress() noexcept; // default: the IPv4 unspecified address, 0.0.0.0

    [[nodiscard]] static IpAddress V4(u8 a, u8 b, u8 c, u8 d) noexcept;
    [[nodiscard]] static IpAddress V6(Span<const u8> bytes) noexcept; // bytes.Size() must be 16

    [[nodiscard]] static Result<IpAddress> Parse(StringView text) noexcept;

    [[nodiscard]] IpVersion Version() const noexcept;
    [[nodiscard]] bool IsV4() const noexcept;
    [[nodiscard]] bool IsV6() const noexcept;

    [[nodiscard]] Span<const u8> Bytes() const noexcept; // 4 bytes (V4) or 16 bytes (V6)

    [[nodiscard]] Result<String> ToString(memory::Allocator& allocator) const;
    [[nodiscard]] Result<String> ToString() const;

    friend bool operator==(const IpAddress& lhs, const IpAddress& rhs) noexcept;
    friend bool operator!=(const IpAddress& lhs, const IpAddress& rhs) noexcept;

private:
    IpVersion version_{ IpVersion::V4 };
    u8 bytes_[16]{};
};

class Endpoint
{
public:
    constexpr Endpoint() noexcept;
    constexpr Endpoint(IpAddress address, u16 port) noexcept;

    [[nodiscard]] static Result<Endpoint> Parse(StringView text) noexcept;

    [[nodiscard]] const IpAddress& Address() const noexcept;
    [[nodiscard]] u16 Port() const noexcept;

    [[nodiscard]] Result<String> ToString(memory::Allocator& allocator) const;
    [[nodiscard]] Result<String> ToString() const;

    friend bool operator==(const Endpoint& lhs, const Endpoint& rhs) noexcept;
    friend bool operator!=(const Endpoint& lhs, const Endpoint& rhs) noexcept;

private:
    IpAddress address_;
    u16 port_{ 0 };
};
```

---

# Memory Layout

`IpAddress`: an `IpVersion` tag (1 byte) plus a fixed 16-byte buffer —
20 bytes total (plus padding), always inline, never heap-allocated.
`Endpoint`: an `IpAddress` plus a `u16` port.

---

# Ownership

Both are plain value types — trivially copyable, no owned resources,
no destructor needed beyond the implicit one.

---

# Error Handling Policy

`Parse` returns `Result<IpAddress>`/`Result<Endpoint>` with
`ErrorCode::ParseError` for any malformed input (wrong number of
groups/octets, an octet or hex group out of range, more than one `::`
in an IPv6 literal, a missing/malformed port in `Endpoint::Parse`,
etc.) — no partial/best-effort result is ever returned. `ToString`
returns `Result<String>` purely because constructing the `String`
result can fail on allocation (`ErrorCode::OutOfMemory`); the
formatting logic itself cannot otherwise fail; V4/V6 factory functions
never fail (matches `Array<T, N>`'s "no allocation, no failure mode"
precedent).

---

# Thread Safety

Both types are immutable value types after construction — safe to read
from multiple threads concurrently, same as any other plain data.

---

# Dependencies

Allowed dependencies:

* Core/Types, Core/Error, Core/Result, Core/Span, Core/String,
  Core/StringView

Forbidden dependencies:

* Any OS/networking header (`<winsock2.h>`, etc.) — that's entirely
  `Socket.cpp`'s job. `IpAddress`/`Endpoint` must stay compilable and
  testable on any platform, the same way `Path` stayed OS-free while
  `File` became the Windows-only counterpart.

---

# Extensibility

Future additions may include:

* A DNS resolver type, producing `Vector<IpAddress>` from a hostname —
  a distinctly different (blocking, network-dependent) operation kept
  separate from this pure value type, per Non-Goals.
* IPv4-mapped IPv6 notation and zone ID support, if a concrete need
  shows up.

Future additions must **not** introduce:

* Allocation on the parse/compare/`Bytes()` path.
* Leniency on the leading-zero-octet ambiguity described in Non-Goals.

---

# Acceptance Criteria

* Public API implemented exactly as specified.
* Pure logic, zero OS dependency — **fully verified**: `g++`/
  `clang++ -std=c++17 -fno-exceptions -Wall -Wextra -Wpedantic -Werror`
  (+ `-Wc++20-extensions` on clang), clean under ASan+UBSan and
  `valgrind --leak-check=full`, plus an `-O2` pass — the same bar as
  `Path`.

---

# Implementation Status

* Architecture: Approved
* Public API: Frozen
* Header/Implementation: Complete (`IpAddress.h`/`.inl`)
* Tests: `IpAddressTest.cpp` — sandbox-verified, covers V4/V6 parse
  round-trips, `::` compression edge cases, the leading-zero-octet
  rejection, and `Endpoint::Parse`'s bracketed-V6 and plain-V4 forms.
