// Standalone correctness test for ErasedCallable — the type-erasure
// helper Thread::Create/ThreadPool::Submit both use to hand an arbitrary
// Callable across a `void(*)(void*)`-shaped OS boundary. Unlike
// Thread/ThreadPool themselves, this is pure C++ with zero OS
// dependency, so — like Path — it gets full sandbox verification:
// g++/clang++ under the real project constraints, ASan+UBSan, valgrind,
// -O2. Compiled under C++17/exceptions-disabled from the start, per the
// user's standing instruction to always target the real mach build's
// constraints.

#include "ErasedCallable.h"

#include <cstdio>
#include <utility>

#include "memory/DefaultAllocator.h"

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

void Test_ErasedCallable_InvokeRunsCallableAndFrees()
{
    std::printf("Test_ErasedCallable_InvokeRunsCallableAndFrees...\n");

    DefaultAllocator allocator;
    bool ran = false;

    Result<detail::ErasedCallable> erased =
        detail::MakeErasedCallable(allocator, [&ran]() { ran = true; });

    CHECK(erased.HasValue());
    CHECK(!ran); // not invoked yet — only allocated and constructed

    erased.Value().invoke(erased.Value().closure);

    CHECK(ran);
    // No further call to invoke/destroy — invoke() already freed the
    // closure. Leak-checked via valgrind in the verification pass below.
}

void Test_ErasedCallable_DestroyWithoutInvokingNeverRunsCallable()
{
    std::printf("Test_ErasedCallable_DestroyWithoutInvokingNeverRunsCallable...\n");

    DefaultAllocator allocator;
    bool ran = false;

    Result<detail::ErasedCallable> erased =
        detail::MakeErasedCallable(allocator, [&ran]() { ran = true; });

    CHECK(erased.HasValue());

    erased.Value().destroy(erased.Value().closure);

    CHECK(!ran); // the whole point of the cleanup-without-running path
}

void Test_ErasedCallable_CapturesMultipleValuesAndMovesThem()
{
    std::printf("Test_ErasedCallable_CapturesMultipleValuesAndMovesThem...\n");

    DefaultAllocator allocator;
    int sum = 0;

    // A move-only capture (via a lambda holding a small owned value)
    // exercises that MakeErasedCallable moves the Callable in rather
    // than requiring it to be copyable.
    struct MoveOnly
    {
        MoveOnly() = default;
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&&) noexcept = default;
        MoveOnly& operator=(MoveOnly&&) noexcept = default;
        int value{ 7 };
    };

    MoveOnly holder;

    Result<detail::ErasedCallable> erased = detail::MakeErasedCallable(
        allocator,
        [&sum, holder = std::move(holder)]() mutable { sum += holder.value; });

    CHECK(erased.HasValue());

    erased.Value().invoke(erased.Value().closure);

    CHECK(sum == 7);
}

void Test_ErasedCallable_AllocationFailureReportsOom()
{
    std::printf("Test_ErasedCallable_AllocationFailureReportsOom...\n");

    FailingAllocator allocator(/*failAfterNCalls=*/0); // fails immediately

    Result<detail::ErasedCallable> erased =
        detail::MakeErasedCallable(allocator, []() {});

    CHECK(erased.HasError());
    CHECK(erased.Error() == ErrorCode::OutOfMemory);
}

} // namespace

int main()
{
    Test_ErasedCallable_InvokeRunsCallableAndFrees();
    Test_ErasedCallable_DestroyWithoutInvokingNeverRunsCallable();
    Test_ErasedCallable_CapturesMultipleValuesAndMovesThem();
    Test_ErasedCallable_AllocationFailureReportsOom();

    if (g_failures == 0)
    {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }

    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
