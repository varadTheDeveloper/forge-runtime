// Standalone correctness test for Queue<T> — same role as
// StringTest.cpp/VectorSmokeTest.cpp.

#include "Queue.h"

#include <cstdio>
#include <utility>

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

void Test_Queue_PushPopOrderIsFifo()
{
    std::printf("Test_Queue_PushPopOrderIsFifo...\n");

    Queue<int> queue;

    CHECK(queue.Empty());

    CHECK(!queue.Push(1).HasError());
    CHECK(!queue.Push(2).HasError());
    CHECK(!queue.Push(3).HasError());

    CHECK(queue.Size() == 3);
    CHECK(queue.Front() == 1);
    CHECK(queue.Back() == 3);

    queue.Pop();
    CHECK(queue.Front() == 2);
    CHECK(queue.Size() == 2);

    queue.Pop();
    queue.Pop();
    CHECK(queue.Empty());
}

void Test_Queue_WrapsAroundCorrectly()
{
    std::printf("Test_Queue_WrapsAroundCorrectly...\n");

    // Push and pop repeatedly without ever letting size_ exceed capacity_,
    // so the same physical slots get reused via wraparound — this is the
    // scenario a naive (non-circular) implementation gets wrong.
    Queue<int> queue;
    queue.Reserve(4).Ignore();

    for (int round = 0; round < 10; ++round)
    {
        CHECK(!queue.Push(round).HasError());
        CHECK(queue.Front() == round);
        queue.Pop();
    }

    CHECK(queue.Empty());
    CHECK(queue.Capacity() == 4); // never had to grow past the initial reservation

    // Now build up a few elements while wrapped, and confirm FIFO order
    // still holds across the wrap boundary.
    queue.Push(100).Ignore();
    queue.Push(101).Ignore();
    queue.Push(102).Ignore();

    CHECK(queue.Front() == 100);
    queue.Pop();
    queue.Push(103).Ignore(); // wraps around the 4-slot buffer

    CHECK(queue.Size() == 3);
    CHECK(queue.Front() == 101);
    queue.Pop();
    CHECK(queue.Front() == 102);
    queue.Pop();
    CHECK(queue.Front() == 103);
    queue.Pop();
    CHECK(queue.Empty());
}

void Test_Queue_GrowthPreservesOrder()
{
    std::printf("Test_Queue_GrowthPreservesOrder...\n");

    Queue<int> queue;

    for (int i = 0; i < 50; ++i)
    {
        CHECK(!queue.Push(i).HasError());
    }

    CHECK(queue.Size() == 50);

    for (int i = 0; i < 50; ++i)
    {
        CHECK(queue.Front() == i);
        queue.Pop();
    }

    CHECK(queue.Empty());
}

void Test_Queue_CopyIsIndependent()
{
    std::printf("Test_Queue_CopyIsIndependent...\n");

    Queue<int> original;
    original.Push(1).Ignore();
    original.Push(2).Ignore();
    original.Pop(); // advance head_ so the copy has to handle a non-zero head_

    Queue<int> copy(original);
    copy.Push(3).Ignore();

    CHECK(copy.Size() == 2);
    CHECK(copy.Front() == 2);
    CHECK(copy.Back() == 3);

    CHECK(original.Size() == 1); // unaffected by mutating the copy
    CHECK(original.Front() == 2);
}

void Test_Queue_MoveStealsSource()
{
    std::printf("Test_Queue_MoveStealsSource...\n");

    Queue<int> source;
    source.Push(1).Ignore();
    source.Push(2).Ignore();

    Queue<int> moved(std::move(source));

    CHECK(moved.Size() == 2);
    CHECK(moved.Front() == 1);
    CHECK(source.Empty());
}

void Test_Queue_IndexingWalksLogicalOrder()
{
    std::printf("Test_Queue_IndexingWalksLogicalOrder...\n");

    // Added in Phase 6 for Forge's microtask queue, which needs to trace
    // every pending element's GC root, not just Front()/Back() — force a
    // wraparound first (same technique Test_Queue_WrapsAroundCorrectly
    // uses) so operator[] is exercised across the physical-buffer wrap
    // boundary, not just the easy non-wrapped case.
    Queue<int> queue;
    queue.Reserve(4).Ignore();

    queue.Push(10).Ignore();
    queue.Push(11).Ignore();
    queue.Push(12).Ignore();
    queue.Pop();               // head_ advances past slot 0
    queue.Push(13).Ignore();   // wraps into the now-free slot 0

    CHECK(queue.Size() == 3);

    // Logical order (front-to-back) should read 11, 12, 13 regardless of
    // where each value physically landed in the 4-slot ring buffer.
    CHECK(queue[0] == 11);
    CHECK(queue[1] == 12);
    CHECK(queue[2] == 13);

    // operator[](0)/operator[](Size()-1) must alias the same elements
    // Front()/Back() do.
    CHECK(&queue[0] == &queue.Front());
    CHECK(&queue[queue.Size() - 1] == &queue.Back());

    // Mutating through operator[] is visible via Front()/Back() too —
    // it's a real reference, not a copy.
    queue[1] = 99;
    CHECK(queue.Front() == 11);
    queue.Pop();
    CHECK(queue.Front() == 99);

    const Queue<int>& constQueue = queue;
    CHECK(constQueue[0] == 99);
}

void Test_Queue_PushReportsOomWithoutCorruption()
{
    std::printf("Test_Queue_PushReportsOomWithoutCorruption...\n");

    FailingAllocator allocator(/*failAfterNCalls=*/1);
    Queue<int> queue(allocator);

    CHECK(!queue.Push(1).HasError());

    bool sawFailure = false;
    for (int i = 0; i < 64; ++i)
    {
        if (queue.Push(i).HasError())
        {
            sawFailure = true;
            break;
        }
    }

    CHECK(sawFailure);
    CHECK(queue.Front() == 1); // whatever succeeded is still intact
}

} // namespace

int main()
{
    Test_Queue_PushPopOrderIsFifo();
    Test_Queue_WrapsAroundCorrectly();
    Test_Queue_GrowthPreservesOrder();
    Test_Queue_CopyIsIndependent();
    Test_Queue_MoveStealsSource();
    Test_Queue_IndexingWalksLogicalOrder();
    Test_Queue_PushReportsOomWithoutCorruption();

    if (g_failures == 0)
    {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }

    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
