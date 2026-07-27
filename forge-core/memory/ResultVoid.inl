#pragma once

namespace forge::core
{
inline void Result<void>::Ignore() const noexcept
{
}
inline Result<void>::Result()
    : has_value_(true)
{
}

inline Result<void>::Result(
    const Failure& failure)
    : error_(failure.Error()),
      has_value_(false)
{
}

inline Result<void>::Result(
    Failure&& failure)
    : error_(failure.Error()),
      has_value_(false)
{
}

inline Result<void>::Result(
    const Result& other) = default;

inline Result<void>::Result(
    Result&& other) noexcept = default;

inline Result<void>::~Result() = default;

inline Result<void>& Result<void>::operator=(
    const Result& other) = default;

inline Result<void>& Result<void>::operator=(
    Result&& other) noexcept = default;

inline bool Result<void>::HasValue() const noexcept
{
    return has_value_;
}

inline bool Result<void>::HasError() const noexcept
{
    return !has_value_;
}

inline Result<void>::operator bool() const noexcept
{
    return has_value_;
}

inline class Error& Result<void>::Error() noexcept
{
    return error_;
}

inline const class Error& Result<void>::Error() const noexcept
{
    return error_;
}

} // namespace forge::core