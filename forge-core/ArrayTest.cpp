// Standalone correctness test for Array<T, N> — same role as
// StringTest.cpp/VectorSmokeTest.cpp: a throwaway verification harness,
// not part of the production moz.build build. Compiled under the real
// project constraints (C++17, exceptions disabled — see HISTORY.md).

#include "Array.h"

#include <cstdio>

using namespace forge::core;

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                          \
    do                                                                       \
    {                                                                        \
        if (!(cond))                                                        \
        {                                                                    \
            std::fprintf(stderr, "CHECK FAILED at %s:%d: %s\n", __FILE__,    \
                          __LINE__, #cond);                                  \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

void Test_Array_BasicAccess()
{
    std::printf("Test_Array_BasicAccess...\n");

    Array<int, 5> values{ { 10, 20, 30, 40, 50 } };

    CHECK(values.Size() == 5);
    CHECK(!values.Empty());
    CHECK(values[0] == 10);
    CHECK(values.Front() == 10);
    CHECK(values.Back() == 50);
    CHECK(values.Data() != nullptr);

    int sum = 0;

    for (int v : values)
    {
        sum += v;
    }

    CHECK(sum == 150);
}

void Test_Array_MutationAndFill()
{
    std::printf("Test_Array_MutationAndFill...\n");

    Array<int, 4> values{};
    values.Fill(7);

    CHECK(values[0] == 7);
    CHECK(values[3] == 7);

    values[2] = 42;
    CHECK(values[2] == 42);
    CHECK(values.Back() == 7); // only index 2 was mutated
}

void Test_Array_Swap()
{
    std::printf("Test_Array_Swap...\n");

    Array<int, 3> a{ { 1, 2, 3 } };
    Array<int, 3> b{ { 4, 5, 6 } };

    a.Swap(b);

    CHECK(a[0] == 4 && a[1] == 5 && a[2] == 6);
    CHECK(b[0] == 1 && b[1] == 2 && b[2] == 3);
}

void Test_Array_Equality()
{
    std::printf("Test_Array_Equality...\n");

    Array<int, 3> a{ { 1, 2, 3 } };
    Array<int, 3> b{ { 1, 2, 3 } };
    Array<int, 3> c{ { 1, 2, 4 } };

    CHECK(a == b);
    CHECK(a != c);
    CHECK(!(a == c));
}

void Test_Array_ZeroSize()
{
    std::printf("Test_Array_ZeroSize...\n");

    Array<int, 0> empty;

    CHECK(empty.Size() == 0);
    CHECK(empty.Empty());
    CHECK(empty.Data() == nullptr);
    CHECK(empty.begin() == empty.end());

    Array<int, 0> otherEmpty;
    CHECK(empty == otherEmpty);
}

} // namespace

int main()
{
    Test_Array_BasicAccess();
    Test_Array_MutationAndFill();
    Test_Array_Swap();
    Test_Array_Equality();
    Test_Array_ZeroSize();

    if (g_failures == 0)
    {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }

    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
