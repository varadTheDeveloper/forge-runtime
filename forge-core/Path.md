# Forge Core Architecture Specification

## Component

**Core / Path**

**Status:** Approved (Architecture Frozen)

---

# Purpose

`Path` represents a filesystem path as a value, and provides pure, portable
manipulation of that value (joining, splitting into parent/filename/
extension, normalizing `.`/`..` segments, checking absolute-vs-relative).

It performs no I/O and touches no OS API — `Path` describes where something
is, `File` (see `File.md`) is what actually reads/writes/opens it. This
split mirrors `StringView`/`String`: one type never touches the OS, the
other (a) the OS itself. Splitting these means `Path` gets the exact same
verification confidence as every other pure forge-core type (Vector,
String, HashMap, ...) — compiled and tested in an ordinary sandbox, no
platform-specific mocking required — while the OS-dependent part of the
filesystem layer stays isolated to `File` alone.

---

# Responsibilities

The `Path` component is responsible for:

* Storing a path as a `String` (UTF-8 internally, per project philosophy).
* Joining path segments with the correct separator.
* Splitting a path into parent directory / filename / stem / extension.
* Normalizing `.` and `..` segments and repeated/mixed separators.
* Reporting whether a path is absolute or relative.
* Recognizing a Windows drive-letter root (`C:`) as part of "absolute",
  since the only backend `File` has (see `File.md`) is Windows.

---

# Non-Goals

`Path` is **not** responsible for:

* Any filesystem I/O (existence checks, reads, writes, directory
  listing) — that is `File`'s job.
* Resolving symlinks, canonicalizing against the actual filesystem, or
  anything else that requires asking the OS a question. Normalization
  here is purely textual (it operates on the string, not the disk).
* Path comparison that accounts for case-insensitivity or OS-specific
  equivalence rules (e.g. Windows treating `C:\a` and `c:\A` as the same
  location) — comparison is exact string comparison via the underlying
  `String`/`StringView` equality, full stop.
* Percent-encoding/URL semantics — a `Path` is a filesystem path, not a
  URL.

---

# Design Goals

* Built on `String`/`StringView`, not `std::string`/`std::filesystem`.
* Constructors never allocate except where `String`'s own rules already
  require it (mirrors `String`'s own `Create(...)` convention).
* No exceptions; fallible operations return `Result<T>`.
* Store the path with forward slashes (`/`) normalized internally, but
  accept backslashes (`\`) on input — Windows accepts both at the API
  boundary, and normalizing on input means every other method (Join,
  Parent, Extension, ...) only has to reason about one separator.
* Cross-platform in principle (nothing here assumes Windows), even
  though `File`, the only consumer so far, is Windows-only.

---

# Public API

## Path

```
class Path
{
public:
    Path() noexcept;
    explicit Path(memory::Allocator& allocator) noexcept;

    static Result<Path> Create(memory::Allocator& allocator, StringView text);
    static Result<Path> Create(StringView text);

    // Copy/move/assign — same shape as String's.

    [[nodiscard]] StringView View() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;

    [[nodiscard]] bool IsAbsolute() const noexcept;
    [[nodiscard]] bool IsRelative() const noexcept;

    // Parent directory, e.g. "a/b/c.txt" -> "a/b". Root-of-tree paths
    // ("/", "C:/") are their own parent, matching std::filesystem::path's
    // convention rather than returning empty.
    [[nodiscard]] StringView Parent() const noexcept;

    // Final path component, e.g. "a/b/c.txt" -> "c.txt".
    [[nodiscard]] StringView FileName() const noexcept;

    // FileName() without its extension, e.g. "c.txt" -> "c".
    [[nodiscard]] StringView Stem() const noexcept;

    // Extension including the leading dot, e.g. "c.txt" -> ".txt".
    // Empty if FileName() has no dot (or is only a leading dot, e.g.
    // ".gitignore" is treated as having no extension, matching the
    // common convention that a dotfile's name is not itself a stem +
    // extension pair).
    [[nodiscard]] StringView Extension() const noexcept;

    // Appends a `/`-joined segment, allocating as needed. Does not
    // re-normalize the whole path on every call (see RemoveDotSegments).
    [[nodiscard]] Result<void> Append(StringView segment);

    // Returns a new Path equal to `*this` joined with `segment` (does not
    // mutate `*this`) — the free-function counterpart to Append().
    [[nodiscard]] static Result<Path> Join(StringView base, StringView segment);
    [[nodiscard]] static Result<Path> Join(memory::Allocator& allocator, StringView base, StringView segment);

    // Rewrites the path in place, collapsing "." segments, resolving
    // ".." against a preceding real segment where possible (never
    // escaping above a leading ".." or an absolute root — e.g.
    // "/a/../.." normalizes to "/", not something above root), and
    // collapsing repeated/mixed separators. Purely textual — does not
    // touch the filesystem, so it cannot know whether a ".." crosses a
    // symlink.
    Result<void> Normalize();

    void Swap(Path& other) noexcept;

    // Comparison via the underlying StringView, exact byte comparison
    // (see Non-Goals re: case sensitivity).
    friend bool operator==(const Path& lhs, const Path& rhs) noexcept;
    friend bool operator!=(const Path& lhs, const Path& rhs) noexcept;

    operator StringView() const noexcept;

private:
    String value_;
};
```

---

# Memory Layout

A single `String` member — same size/shape as `String` itself, no
additional state.

---

# Ownership

`Path` owns its own UTF-8 byte buffer through the same `String`/
`Allocator` mechanism every other forge-core owning type uses. It owns no
OS handle, file descriptor, or other external resource — that distinction
is exactly what separates it from `File`.

---

# Error Handling Policy

Construction that requires allocation goes through `Path::Create(...)`,
matching `String::Create(...)`. `Append`/`Normalize` return
`Result<void>` since both may need to grow the underlying buffer.
Never throws; exceptions are disabled in the real build regardless.

---

# Thread Safety

Same as every other forge-core value type: safe to use from a single
thread; independent instances may be freely moved between threads.
Sharing one `Path` across threads without external synchronization is
the caller's responsibility, same as `Vector`/`String`.

---

# Dependencies

Allowed dependencies:

* Core/Types
* Core/String, Core/StringView
* Core/Result

Forbidden dependencies:

* Any OS/platform-specific header (`<windows.h>`, POSIX headers, ...) —
  that is exactly what `File` exists to isolate.
* Threading, Networking.

---

# Extensibility

Future additions may include:

* `Path::Canonicalize()` — an I/O-performing variant that asks the
  actual filesystem to resolve symlinks; this would live on `File`
  (or a free function taking a `Path` and returning a new one), not on
  `Path` itself, to keep `Path` I/O-free.
* Case-insensitive comparison helpers, if a concrete Windows-path-
  equivalence need arises.

Future additions must **not** introduce:

* Any OS API call from within `Path` itself.
* Exceptions.

---

# Acceptance Criteria

* Public API implemented exactly as specified.
* Unit tests pass under `g++`/`clang++ -std=c++17 -fno-exceptions -Wall
  -Wextra -Wpedantic -Werror` (+ `-Wc++20-extensions` on clang), clean
  under ASan+UBSan and `valgrind --leak-check=full`.
* No OS-specific headers appear anywhere in `Path.h`/`Path.inl`.

---

# Implementation Status

* Architecture: Approved
* Public API: Frozen
* Header: Pending
* Implementation: Pending
* Tests: Pending
