#pragma once

#include "Path.h"
#include "Result.h"
#include "Span.h"
#include "String.h"
#include "Types.h"
#include "memory/ResultVoid.h"
#include "memory/Vector.h"

namespace forge::core
{

enum class FileMode : u8
{
    Read,       // must exist; read-only
    Write,      // create or truncate; write-only
    ReadWrite,  // create or truncate; read+write
    Append,     // create if missing; write-only; the handle starts
                // positioned at end-of-file so writes append
    CreateNew,  // fails with ErrorCode::AlreadyExists if the file exists
};

enum class SeekOrigin : u8
{
    Begin,
    Current,
    End,
};

/// Synchronous file I/O. See File.md for the full spec, in particular:
/// Windows/Win32-only (no other backend exists), and deliberately
/// synchronous (no IocpLoop dependency) — an async variant is future
/// work for whenever Runtime Integration needs one.
///
/// Move-only, like UniquePtr<T>, not copyable-by-value like Vector<T>/
/// String — a Win32 HANDLE has no cheap, well-defined "copy" operation.
///
/// Cannot be compiled or run in the sandbox this was written in (no
/// Windows SDK, no working MinGW cross-compiler available) — verified
/// by careful manual review only. Needs a real mach build (or a
/// standalone Visual Studio smoke test) to go from "implemented" to
/// "confirmed working".
class [[nodiscard]] File
{
public:

    using SizeType = forge::core::Size;

    File() noexcept;

    File(const File&) = delete;
    File& operator=(const File&) = delete;

    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;

    ~File() noexcept;

    [[nodiscard]]
    static Result<File> Open(
        const Path& path,
        FileMode mode);

    [[nodiscard]]
    bool IsOpen() const noexcept;

    /// Reads up to buffer.Size() bytes. Returns the number actually read
    /// — 0 at end-of-file, which is not itself an error.
    [[nodiscard]]
    Result<SizeType> Read(
        Span<u8> buffer);

    /// Writes buffer.Size() bytes. Returns the number actually written —
    /// a short write is surfaced as-is, not silently retried.
    [[nodiscard]]
    Result<SizeType> Write(
        Span<const u8> buffer);

    [[nodiscard]]
    Result<u64> Seek(
        i64 offset,
        SeekOrigin origin);

    [[nodiscard]]
    Result<u64> Tell();

    [[nodiscard]]
    Result<u64> SizeInBytes();

    void Close() noexcept;

    //==========================================================================
    // Path-only operations — no open handle required.
    //==========================================================================

    [[nodiscard]]
    static Result<bool> Exists(
        const Path& path);

    /// Named `MakeDirectory` rather than `CreateDirectory` deliberately:
    /// <windows.h>'s fileapi.h `#define`s `CreateDirectory` to
    /// `CreateDirectoryA`/`CreateDirectoryW` depending on the UNICODE
    /// build setting, so a method with that exact name gets silently
    /// text-substituted by the preprocessor wherever windows.h has been
    /// included — this is a real bug a real `mach build` caught (the
    /// hand-written mock windows.h used for sandbox verification didn't
    /// replicate the macro, so it slipped through there). Same reasoning
    /// applies to CreateFile/DeleteFile/MoveFile/CopyFile and friends —
    /// avoid those exact names for any of our own symbols.
    [[nodiscard]]
    static Result<void> MakeDirectory(
        const Path& path);

    /// Recursive — creates every missing ancestor directory too, like
    /// `mkdir -p`. Treats an ancestor that already exists as success.
    [[nodiscard]]
    static Result<void> CreateDirectories(
        const Path& path);

    [[nodiscard]]
    static Result<void> Remove(
        const Path& path);

    [[nodiscard]]
    static Result<Vector<u8>> ReadAllBytes(
        const Path& path);

    [[nodiscard]]
    static Result<String> ReadAllText(
        const Path& path);

private:

    explicit File(
        void* handle) noexcept;

private:

    // Kept as void* (an opaque Win32 HANDLE) rather than including
    // <windows.h> here — same reasoning IocpLoop applies to its own
    // OVERLAPPED-derived types: keep the platform header confined to the
    // .cpp, out of every translation unit that just wants to open a
    // file.
    void* handle_{ nullptr };
};

} // namespace forge::core
