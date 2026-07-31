#pragma once

#include <utility>

namespace forge::core
{

//==============================================================================
// Helpers
//==============================================================================

inline bool Path::IsAsciiAlpha(
    char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

inline bool Path::IsRootPath(
    StringView text) noexcept
{
    if (text.Size() == 1 && text[0] == '/')
    {
        return true;
    }

    if (text.Size() == 3 && IsAsciiAlpha(text[0]) && text[1] == ':' && text[2] == '/')
    {
        return true;
    }

    if (text.Size() == 2 && text[0] == '/' && text[1] == '/')
    {
        return true;
    }

    return false;
}

//==============================================================================
// Construction
//==============================================================================

inline Path::Path() noexcept
    :
    value_()
{
}

inline Path::Path(
    memory::Allocator& allocator) noexcept
    :
    value_(allocator)
{
}

inline Result<Path> Path::Create(
    memory::Allocator& allocator,
    StringView text)
{
    Path path(allocator);

    if (Result<void> reserved = path.value_.Reserve(text.Size()); reserved.HasError())
    {
        return Result<Path>(Failure{ reserved.Error() });
    }

    for (Size index = 0; index < text.Size(); ++index)
    {
        char c = text[index];

        if (c == '\\')
        {
            c = '/';
        }

        if (Result<void> appended = path.value_.Append(c); appended.HasError())
        {
            return Result<Path>(Failure{ appended.Error() });
        }
    }

    return Result<Path>(std::move(path));
}

inline Result<Path> Path::Create(
    StringView text)
{
    return Create(memory::GetDefaultAllocator(), text);
}

//==============================================================================
// Observers
//==============================================================================

inline StringView Path::View() const noexcept
{
    return value_.View();
}

inline bool Path::Empty() const noexcept
{
    return value_.Empty();
}

inline bool Path::IsAbsolute() const noexcept
{
    StringView v = value_.View();

    if (v.Size() >= 3 && IsAsciiAlpha(v[0]) && v[1] == ':' && v[2] == '/')
    {
        return true;
    }

    if (v.Size() >= 2 && v[0] == '/' && v[1] == '/')
    {
        return true;
    }

    return false;
}

inline bool Path::IsRelative() const noexcept
{
    return !IsAbsolute();
}

inline StringView Path::Parent() const noexcept
{
    StringView v = value_.View();

    if (v.Empty() || IsRootPath(v))
    {
        return v;
    }

    SizeType end = v.Size();

    if (v[end - 1] == '/')
    {
        --end;
    }

    StringView trimmed = v.Substr(0, end);

    if (trimmed.Empty() || IsRootPath(trimmed))
    {
        return trimmed;
    }

    SizeType lastSlash = trimmed.RFind(StringView("/"));

    if (lastSlash == StringView::kNotFound)
    {
        return StringView(); // no separator at all -> no parent
    }

    StringView candidateParent = trimmed.Substr(0, lastSlash + 1);

    if (IsRootPath(candidateParent))
    {
        return candidateParent; // keep the root's own trailing slash
    }

    return trimmed.Substr(0, lastSlash);
}

inline StringView Path::FileName() const noexcept
{
    StringView v = value_.View();

    if (v.Empty() || IsRootPath(v))
    {
        return StringView();
    }

    SizeType end = v.Size();

    if (v[end - 1] == '/')
    {
        --end;
    }

    StringView trimmed = v.Substr(0, end);

    if (trimmed.Empty() || IsRootPath(trimmed))
    {
        return StringView();
    }

    SizeType lastSlash = trimmed.RFind(StringView("/"));

    if (lastSlash == StringView::kNotFound)
    {
        return trimmed;
    }

    return trimmed.Substr(lastSlash + 1);
}

inline StringView Path::Stem() const noexcept
{
    StringView name = FileName();
    SizeType dot = name.RFind(StringView("."));

    if (dot == StringView::kNotFound || dot == 0)
    {
        return name;
    }

    return name.Substr(0, dot);
}

inline StringView Path::Extension() const noexcept
{
    StringView name = FileName();
    SizeType dot = name.RFind(StringView("."));

    if (dot == StringView::kNotFound || dot == 0)
    {
        return StringView();
    }

    return name.Substr(dot);
}

//==============================================================================
// Modifiers
//==============================================================================

inline Result<void> Path::Append(
    StringView segment)
{
    if (segment.Empty())
    {
        return {};
    }

    const bool needSeparator = !value_.Empty() && value_.View()[value_.Size() - 1] != '/';

    if (needSeparator)
    {
        if (Result<void> result = value_.Append('/'); result.HasError())
        {
            return result;
        }
    }

    for (Size index = 0; index < segment.Size(); ++index)
    {
        char c = segment[index];

        if (c == '\\')
        {
            c = '/';
        }

        if (Result<void> result = value_.Append(c); result.HasError())
        {
            return result;
        }
    }

    return {};
}

inline Result<Path> Path::Join(
    memory::Allocator& allocator,
    StringView base,
    StringView segment)
{
    Result<Path> path = Path::Create(allocator, base);

    if (path.HasError())
    {
        return path;
    }

    if (Result<void> result = path.Value().Append(segment); result.HasError())
    {
        return Result<Path>(Failure{ result.Error() });
    }

    return path;
}

inline Result<Path> Path::Join(
    StringView base,
    StringView segment)
{
    return Join(memory::GetDefaultAllocator(), base, segment);
}

inline Result<void> Path::Normalize()
{
    StringView v = value_.View();

    if (v.Empty())
    {
        return {};
    }

    StringView root;
    StringView rest = v;

    if (IsRootPath(v))
    {
        root = v;
        rest = StringView();
    }
    else if (v.Size() >= 3 && IsAsciiAlpha(v[0]) && v[1] == ':' && v[2] == '/')
    {
        root = v.Substr(0, 3);
        rest = v.Substr(3);
    }
    else if (v[0] == '/')
    {
        root = v.Substr(0, 1);
        rest = v.Substr(1);
    }

    const bool isAbsolute = !root.Empty();

    Vector<StringView> stack; // resolved segments, aliasing the ORIGINAL
                               // buffer — safe as long as nothing mutates
                               // value_ until after this loop finishes.

    SizeType index = 0;

    while (index <= rest.Size())
    {
        SizeType next = rest.Size();

        for (SizeType scan = index; scan < rest.Size(); ++scan)
        {
            if (rest[scan] == '/')
            {
                next = scan;
                break;
            }
        }

        StringView segment = rest.Substr(index, next - index);

        if (!segment.Empty() && segment != StringView("."))
        {
            if (segment == StringView(".."))
            {
                if (!stack.Empty() && stack.Back() != StringView(".."))
                {
                    stack.PopBack();
                }
                else if (!isAbsolute)
                {
                    if (Result<void> result = stack.PushBack(segment); result.HasError())
                    {
                        return result;
                    }
                }
                // else: absolute path, ".." above root is simply dropped.
            }
            else
            {
                if (Result<void> result = stack.PushBack(segment); result.HasError())
                {
                    return result;
                }
            }
        }

        index = next + 1;
    }

    // Build the result into a completely separate scratch String (its
    // own, temporary default-allocator buffer) rather than rewriting
    // value_ in place — `stack`'s StringViews still alias value_'s
    // CURRENT buffer, so mutating value_ (which could reallocate) while
    // still reading from `stack` would be a use-after-free. Copy-
    // assigning the finished result into value_ at the very end (which
    // copies bytes using value_'s OWN allocator, per String's own
    // copy-assignment semantics) means value_'s original allocator is
    // preserved throughout, even though `rebuilt` itself used the
    // default one.
    String rebuilt;

    if (Result<void> result = rebuilt.Append(root); result.HasError())
    {
        return result;
    }

    for (Size i = 0; i < stack.Size(); ++i)
    {
        if (i > 0)
        {
            if (Result<void> result = rebuilt.Append('/'); result.HasError())
            {
                return result;
            }
        }

        if (Result<void> result = rebuilt.Append(stack[i]); result.HasError())
        {
            return result;
        }
    }

    if (rebuilt.Empty())
    {
        // A relative path that fully normalized away, e.g. "." or "a/..".
        if (Result<void> result = rebuilt.Append(StringView(".")); result.HasError())
        {
            return result;
        }
    }

    value_ = rebuilt;

    return {};
}

inline void Path::Swap(
    Path& other) noexcept
{
    value_.Swap(other.value_);
}

inline Path::operator StringView() const noexcept
{
    return View();
}

} // namespace forge::core
