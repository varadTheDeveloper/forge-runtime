// Standalone correctness test for Stack<T> — same role as
// StringTest.cpp/VectorSmokeTest.cpp.

#include "Stack.h"

#include <cstdio>

using namespace forge::core;
using namespace forge::core::memory;

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

class FailingAllocator final : public Allocator
{
public:
    explicit FailingAllocator(int failAfterNCalls) : failAfterNCalls_(failAfterNCalls) {}

    [[nodiscard]] void* Allocate(Size size, Size alignment) noexcept override
    {
        if (callCount_ >= failAfterNCalls_)
        {
            return nullptr;
        }
        ++callCount_;
        return DefaultAllocator{}.Allocate(size, alignment);
    }

    void Deallocate(void* memory, Size size, Size alignment) noexcept override
    {
        DefaultAllocator{}.Deallocate(memory, size, alignment);
    }

private:
    int failAfterNCalls_;
    int callCount_{ 0 };
};

void Test_Stack_PushPopOrderIsLifo()
{
    std::printf("Test_Stack_PushPopOrderIsLifo...\n");

    Stack<int> stack;

    CHECK(stack.Empty());

    CHECK(!stack.Push(1).HasError());
    CHECK(!stack.Push(2).HasError());
    CHECK(!stack.Push(3).HasError());

    CHECK(stack.Size() == 3);
    CHECK(stack.Top() == 3);

    stack.Pop();
    CHECK(stack.Top() == 2);
    CHECK(stack.Size() == 2);

    stack.Pop();
    stack.Pop();
    CHECK(stack.Empty());
}

void Test_Stack_EmplaceAndClear()
{
    std::printf("Test_Stack_EmplaceAndClear...\n");

    Stack<int> stack;

    CHECK(!stack.Emplace(10).HasError());
    CHECK(!stack.Emplace(20).HasError());
    CHECK(stack.Top() == 20);

    stack.Clear();
    CHECK(stack.Empty());
    CHECK(stack.Size() == 0);
}

void Test_Stack_CopyIsIndependent()
{
    std::printf("Test_Stack_CopyIsIndependent...\n");

    Stack<int> original;
    original.Push(1).Ignore();
    original.Push(2).Ignore();

    Stack<int> copy(original);
    copy.Push(3).Ignore();

    CHECK(copy.Size() == 3);
    CHECK(original.Size() == 2); // unaffected by mutating the copy
    CHECK(original.Top() == 2);
}

void Test_Stack_MoveStealsSource()
{
    std::printf("Test_Stack_MoveStealsSource...\n");

    Stack<int> source;
    source.Push(1).Ignore();
    source.Push(2).Ignore();

    Stack<int> moved(std::move(source));

    CHECK(moved.Size() == 2);
    CHECK(moved.Top() == 2);
    CHECK(source.Empty()); // Vector<T>'s moved-from state
}

void Test_Stack_PushReportsOomWithoutCorruption()
{
    std::printf("Test_Stack_PushReportsOomWithoutCorruption...\n");

    FailingAllocator allocator(/*failAfterNCalls=*/1); // first grow succeeds, next fails
    Stack<int> stack(allocator);

    CHECK(!stack.Push(1).HasError());

    bool sawFailure = false;
    for (int i = 0; i < 64; ++i)
    {
        if (stack.Push(i).HasError())
        {
            sawFailure = true;
            break;
        }
    }

    CHECK(sawFailure);
    CHECK(stack.Top() == 1); // whatever succeeded is still intact and readable
}

} // namespace

int main()
{
    Test_Stack_PushPopOrderIsLifo();
    Test_Stack_EmplaceAndClear();
    Test_Stack_CopyIsIndependent();
    Test_Stack_MoveStealsSource();
    Test_Stack_PushReportsOomWithoutCorruption();

    if (g_failures == 0)
    {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }

    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
