#pragma once

#include <utility>

#include "memory/ResultFwd.h"
#include "Failure.h"
#include "ResultStorage.h"

namespace forge::core
{

template<typename T>
class [[nodiscard]] Result
{
public:
  void Ignore() const noexcept;
    //==========================================================================
    // Construction
    //==========================================================================

    explicit Result(const T& value);

    explicit Result(T&& value);

    explicit Result(const Failure& failure);

    explicit Result(Failure&& failure);

    Result(const Result& other);

    Result(Result&& other) noexcept;

    ~Result();

    //==========================================================================
    // Assignment
    //==========================================================================

    Result& operator=(const Result& other);

    Result& operator=(Result&& other) noexcept;

    //==========================================================================
    // State
    //==========================================================================

    [[nodiscard]]
    bool HasValue() const noexcept;

    [[nodiscard]]
    bool HasError() const noexcept;

    [[nodiscard]]
    explicit operator bool() const noexcept;

    //==========================================================================
    // Value Access
    //==========================================================================

    [[nodiscard]]
    T& Value() noexcept;

    [[nodiscard]]
    const T& Value() const noexcept;

    //==========================================================================
    // Error Access
    //==========================================================================

    [[nodiscard]]
    class Error& Error() noexcept;

    [[nodiscard]]
    const class Error& Error() const noexcept;

    //==========================================================================
    // Operators
    //==========================================================================

    [[nodiscard]]
    T& operator*() noexcept;

    [[nodiscard]]
    const T& operator*() const noexcept;

    [[nodiscard]]
    T* operator->() noexcept;

    [[nodiscard]]
    const T* operator->() const noexcept;

private:

    //==========================================================================
    // Helpers
    //==========================================================================

    [[nodiscard]]
    T* ValuePtr() noexcept;

    [[nodiscard]]
    const T* ValuePtr() const noexcept;

    [[nodiscard]]
    forge::core::Error* ErrorPtr() noexcept;

    [[nodiscard]]
    const forge::core::Error* ErrorPtr() const noexcept;

private:

    ResultStorage<T> storage_;

    bool has_value_{ false };
};

} // namespace forge::core

#include "Result.inl"