#include <type_traits>
#include <gtest/gtest.h>
#include "Error.h"

using namespace forge::core;

//------------------------------------------------------------------------------
// Compile-time checks
//------------------------------------------------------------------------------

static_assert(std::is_trivially_copyable_v<Error>);
static_assert(std::is_standard_layout_v<Error>);
static_assert(std::is_nothrow_copy_constructible_v<Error>);
static_assert(std::is_nothrow_move_constructible_v<Error>);

//------------------------------------------------------------------------------
// Runtime tests (GoogleTest)
//------------------------------------------------------------------------------

TEST(Error, DefaultConstruction)
{
    Error error;

    EXPECT_FALSE(error);
    EXPECT_EQ(error.Code(), ErrorCode::None);
    EXPECT_EQ(error.NativeCode(), 0);
}

TEST(Error, ConstructWithCode)
{
    Error error(ErrorCode::NotFound);

    EXPECT_TRUE(error);
    EXPECT_EQ(error.Code(), ErrorCode::NotFound);
    EXPECT_EQ(error.NativeCode(), 0);
}

TEST(Error, ConstructWithNativeCode)
{
    Error error(ErrorCode::PermissionDenied, 13);

    EXPECT_TRUE(error);
    EXPECT_EQ(error.Code(), ErrorCode::PermissionDenied);
    EXPECT_EQ(error.NativeCode(), 13);
}

TEST(Error, Equality)
{
    Error a(ErrorCode::Timeout, 100);
    Error b(ErrorCode::Timeout, 100);
    Error c(ErrorCode::Timeout, 200);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(Error, CompareWithErrorCode)
{
    Error error(ErrorCode::InvalidArgument);

    EXPECT_TRUE(error == ErrorCode::InvalidArgument);
    EXPECT_TRUE(ErrorCode::InvalidArgument == error);
    EXPECT_FALSE(error == ErrorCode::Timeout);
}