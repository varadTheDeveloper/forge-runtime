// Standalone correctness test for HashSet<K> — same role as
// StringTest.cpp/VectorSmokeTest.cpp. Kept lighter than HashMapTest.cpp:
// HashSet is a thin wrapper over HashMap<K, detail::Unit>, so the hard
// probing/growth/tombstone logic is already covered there — this file
// focuses on the set-shaped API surface itself.

#include "HashSet.h"
#include "StringView.h"

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

void Test_HashSet_InsertContainsErase()
{
    std::printf("Test_HashSet_InsertContainsErase...\n");

    HashSet<i32> set;

    CHECK(set.Empty());
    CHECK(!set.Contains(1));

    Result<bool> inserted = set.Insert(1);
    CHECK(inserted.HasValue() && inserted.Value() == true);
    CHECK(set.Contains(1));
    CHECK(set.Size() == 1);

    Result<bool> insertedAgain = set.Insert(1);
    CHECK(insertedAgain.HasValue() && insertedAgain.Value() == false); // already present
    CHECK(set.Size() == 1); // no duplicate

    CHECK(set.Erase(1));
    CHECK(!set.Contains(1));
    CHECK(!set.Erase(1)); // already gone
}

void Test_HashSet_StringViewKeys()
{
    std::printf("Test_HashSet_StringViewKeys...\n");

    HashSet<StringView> set;

    set.Insert(StringView("red")).Ignore();
    set.Insert(StringView("green")).Ignore();
    set.Insert(StringView("blue")).Ignore();

    CHECK(set.Contains(StringView("green")));
    CHECK(!set.Contains(StringView("yellow")));
    CHECK(set.Size() == 3);
}

void Test_HashSet_Iteration()
{
    std::printf("Test_HashSet_Iteration...\n");

    HashSet<i32> set;
    set.Insert(1).Ignore();
    set.Insert(2).Ignore();
    set.Insert(3).Ignore();

    bool seen[4] = { false, false, false, false };
    Size count = 0;

    for (i32 key : set)
    {
        CHECK(key >= 1 && key <= 3);
        seen[key] = true;
        ++count;
    }

    CHECK(count == 3);
    CHECK(seen[1] && seen[2] && seen[3]);
}

void Test_HashSet_GrowthPreservesAllEntries()
{
    std::printf("Test_HashSet_GrowthPreservesAllEntries...\n");

    HashSet<i32> set;

    constexpr i32 kCount = 500;

    for (i32 i = 0; i < kCount; ++i)
    {
        CHECK(!set.Insert(i).HasError());
    }

    CHECK(set.Size() == static_cast<Size>(kCount));

    for (i32 i = 0; i < kCount; ++i)
    {
        CHECK(set.Contains(i));
    }
}

void Test_HashSet_CopyIsIndependent()
{
    std::printf("Test_HashSet_CopyIsIndependent...\n");

    HashSet<i32> original;
    original.Insert(1).Ignore();
    original.Insert(2).Ignore();

    HashSet<i32> copy(original);
    copy.Insert(3).Ignore();

    CHECK(copy.Size() == 3);
    CHECK(original.Size() == 2); // unaffected by mutating the copy
    CHECK(!original.Contains(3));
}

void Test_HashSet_MoveStealsSource()
{
    std::printf("Test_HashSet_MoveStealsSource...\n");

    HashSet<i32> source;
    source.Insert(1).Ignore();
    source.Insert(2).Ignore();

    HashSet<i32> moved(std::move(source));

    CHECK(moved.Size() == 2);
    CHECK(moved.Contains(1));
    CHECK(source.Empty());
}

void Test_HashSet_InsertReportsOomWithoutCorruption()
{
    std::printf("Test_HashSet_InsertReportsOomWithoutCorruption...\n");

    FailingAllocator allocator(/*failAfterNCalls=*/3);
    HashSet<i32> set(allocator);

    bool sawFailure = false;

    for (i32 i = 0; i < 64; ++i)
    {
        if (set.Insert(i).HasError())
        {
            sawFailure = true;
            break;
        }
    }

    CHECK(sawFailure);
    CHECK(set.Size() <= set.Capacity());
}

} // namespace

int main()
{
    Test_HashSet_InsertContainsErase();
    Test_HashSet_StringViewKeys();
    Test_HashSet_Iteration();
    Test_HashSet_GrowthPreservesAllEntries();
    Test_HashSet_CopyIsIndependent();
    Test_HashSet_MoveStealsSource();
    Test_HashSet_InsertReportsOomWithoutCorruption();

    if (g_failures == 0)
    {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }

    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
