#pragma once

#include <memory>
#include <utility>

#include "Assert.h"
#include "Construct.h"
namespace forge::core
{
template<typename T>
inline void Result<T>::Ignore() const noexcept
{
}
//==============================================================================
// Private Helpers
//==============================================================================

template<typename T>
inline T* Result<T>::ValuePtr() noexcept
{
    return &storage_.value;
}

template<typename T>
inline const T* Result<T>::ValuePtr() const noexcept
{
    return &storage_.value;
}

template<typename T>
inline Error* Result<T>::ErrorPtr() noexcept
{
    return &storage_.error;
}

template<typename T>
inline const Error* Result<T>::ErrorPtr() const noexcept
{
    return &storage_.error;
}

//==============================================================================
// Value Constructors
//==============================================================================

template<typename T>
inline Result<T>::Result(
    const T& value)
{
    forge::core::detail::ConstructAt(
        ValuePtr(),
        value);

    has_value_ = true;
}

template<typename T>
inline Result<T>::Result(
    T&& value)
{
    forge::core::detail::ConstructAt(
        ValuePtr(),
        std::move(value));

    has_value_ = true;
}

//==============================================================================
// Failure Constructors
//==============================================================================

template<typename T>
inline Result<T>::Result(
    const Failure& failure)
{
    forge::core::detail::ConstructAt(
        ErrorPtr(),
        failure.Error());

    has_value_ = false;
}

template<typename T>
inline Result<T>::Result(
    Failure&& failure)
{
    forge::core::detail::ConstructAt(
        ErrorPtr(),
        std::move(failure.Error()));

    has_value_ = false;
}

//==============================================================================
// Copy Constructor
//==============================================================================

template<typename T>
inline Result<T>::Result(
    const Result& other)
{
    if (other.has_value_)
    {
        forge::core::detail::ConstructAt(
            ValuePtr(),
            other.Value());

        has_value_ = true;
    }
    else
    {
        forge::core::detail::ConstructAt(
            ErrorPtr(),
            other.Error());

        has_value_ = false;
    }
}

//==============================================================================
// Move Constructor
//==============================================================================

template<typename T>
inline Result<T>::Result(
    Result&& other) noexcept
{
    if (other.has_value_)
    {
        forge::core::detail::ConstructAt(
            ValuePtr(),
            std::move(other.Value()));

        has_value_ = true;
    }
    else
    {
        forge::core::detail::ConstructAt(
            ErrorPtr(),
            other.Error());

        has_value_ = false;
    }
}

//==============================================================================
// Destructor
//==============================================================================

template<typename T>
inline Result<T>::~Result()
{
    if (has_value_)
    {
        std::destroy_at(ValuePtr());
    }
    else
    {
        std::destroy_at(ErrorPtr());
    }
}
//==============================================================================
// Copy Assignment
//==============================================================================

template<typename T>
inline Result<T>& Result<T>::operator=(
    const Result& other)
{
    if (this == &other)
    {
        return *this;
    }

    if (has_value_ && other.has_value_)
    {
        *ValuePtr() = other.Value();
    }
    else if (!has_value_ && !other.has_value_)
    {
        *ErrorPtr() = other.Error();
    }
    else if (has_value_ && !other.has_value_)
    {
        std::destroy_at(ValuePtr());

        forge::core::detail::ConstructAt(
            ErrorPtr(),
            other.Error());

        has_value_ = false;
    }
    else
    {
        std::destroy_at(ErrorPtr());

        forge::core::detail::ConstructAt(
            ValuePtr(),
            other.Value());

        has_value_ = true;
    }

    return *this;
}

//==============================================================================
// Move Assignment
//==============================================================================

template<typename T>
inline Result<T>& Result<T>::operator=(
    Result&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    if (has_value_ && other.has_value_)
    {
        *ValuePtr() = std::move(other.Value());
    }
    else if (!has_value_ && !other.has_value_)
    {
        *ErrorPtr() = other.Error();
    }
    else if (has_value_ && !other.has_value_)
    {
        std::destroy_at(ValuePtr());

        forge::core::detail::ConstructAt(
            ErrorPtr(),
            other.Error());

        has_value_ = false;
    }
    else
    {
        std::destroy_at(ErrorPtr());

        forge::core::detail::ConstructAt(
            ValuePtr(),
            std::move(other.Value()));

        has_value_ = true;
    }

    return *this;
}

//==============================================================================
// State
//==============================================================================

template<typename T>
inline bool Result<T>::HasValue() const noexcept
{
    return has_value_;
}

template<typename T>
inline bool Result<T>::HasError() const noexcept
{
    return !has_value_;
}

template<typename T>
inline Result<T>::operator bool() const noexcept
{
    return has_value_;
}

//==============================================================================
// Value Access
//==============================================================================

template<typename T>
inline T& Result<T>::Value() noexcept
{
    FORGE_ASSERT(has_value_);

    return *ValuePtr();
}

template<typename T>
inline const T& Result<T>::Value() const noexcept
{
    FORGE_ASSERT(has_value_);

    return *ValuePtr();
}

//==============================================================================
// Error Access
//==============================================================================

template<typename T>
inline class Error& Result<T>::Error() noexcept
{
    FORGE_ASSERT(!has_value_);

    return *ErrorPtr();
}

template<typename T>
inline const class Error& Result<T>::Error() const noexcept
{
    FORGE_ASSERT(!has_value_);

    return *ErrorPtr();
}

//==============================================================================
// Operators
//==============================================================================

template<typename T>
inline T& Result<T>::operator*() noexcept
{
    return Value();
}

template<typename T>
inline const T& Result<T>::operator*() const noexcept
{
    return Value();
}

template<typename T>
inline T* Result<T>::operator->() noexcept
{
    return ValuePtr();
}

template<typename T>
inline const T* Result<T>::operator->() const noexcept
{
    return ValuePtr();
}

} // namespace forge::core
// NOTE: everything above this line must stay inside namespace forge::core.
// (This file previously closed the namespace right after the Failure
// constructors, silently leaving the copy/move ctors, assignment operators,
// HasValue/Value/Error accessors and operator* /-> defined at global scope,
// which does not compile since Result<T> is a forge::core member.)