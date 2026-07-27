#include <cassert>
#include <cstdio>
#include "forge-core/Types.h"
#include "forge-core/Error.h"
#include "forge-core/Failure.h"
#include "forge-core/Result.h"
#include "forge-core/memory/ResultVoid.h"
#include "forge-core/memory/Allocator.h"
#include "forge-core/memory/DefaultAllocator.h"
#include "forge-core/memory/UniquePtr.h"
#include "forge-core/memory/MakeUnique.h"
#include "forge-core/memory/Vector.h"

using namespace forge::core;
using namespace forge::core::memory;

// Allocator that always fails, to exercise the OutOfMemory paths.
class FailingAllocator final : public Allocator {
public:
    void* Allocate(Size, Size) noexcept override { return nullptr; }
    void Deallocate(void*, Size, Size) noexcept override {}
};

struct Widget {
    int x;
    static int alive;
    Widget(int v) : x(v) { ++alive; }
    Widget(const Widget& o) : x(o.x) { ++alive; }
    Widget(Widget&& o) noexcept : x(o.x) { ++alive; }
    ~Widget() { --alive; }
};
int Widget::alive = 0;

int main() {
    // --- Result<T> basic ---
    Result<int> r(5);
    assert(r.HasValue() && !r.HasError());
    assert(r.Value() == 5);

    Result<int> e{ Failure(Error(ErrorCode::NotFound)) };
    assert(!e.HasValue() && e.HasError());
    assert(e.Error().Code() == ErrorCode::NotFound);

    // copy/move of Result<T>
    Result<int> e2 = e;
    assert(e2.HasError() && e2.Error().Code() == ErrorCode::NotFound);
    Result<int> e3 = std::move(e2);
    assert(e3.HasError());

    // --- Result<void> ---
    Result<void> rv;
    assert(rv.HasValue() && !rv.HasError());
    Result<void> rve{ Failure(Error(ErrorCode::Timeout)) };
    assert(rve.HasError() && rve.Error().Code() == ErrorCode::Timeout);

    // --- Vector basic ops ---
    {
        Vector<int> v;
        assert(v.Empty());
        assert(v.PushBack(1));
        assert(v.PushBack(2));
        assert(v.PushBack(3));
        assert(v.Size() == 3);
        assert(v[0] == 1 && v[1] == 2 && v[2] == 3);
        assert(v.Front() == 1);
        assert(v.Back() == 3);

        Vector<int> copy(v);
        assert(copy.Size() == 3);
        copy[0] = 99;
        assert(v[0] == 1); // deep copy, independent storage

        Vector<int> moved(std::move(v));
        assert(moved.Size() == 3);
        assert(v.Size() == 0); // NOLINT: testing moved-from state

        Vector<int> assigned;
        assigned = moved;
        assert(assigned.Size() == 3 && assigned[2] == 3);

        v.PopBack();
    }

    // --- Vector with non-trivial type + EmplaceBack ---
    {
        Vector<Widget> v;
        assert(v.EmplaceBack(10));
        assert(v.EmplaceBack(20));
        assert(v.Size() == 2);
        assert(v[0].x == 10 && v[1].x == 20);
        assert(Widget::alive == 2);
    }
    assert(Widget::alive == 0); // destructors ran correctly

    // --- Reserve() failure path with a failing allocator ---
    {
        FailingAllocator failing;
        Vector<int> v(failing);
        Result<void> res = v.Reserve(16);
        assert(res.HasError());
        assert(res.Error().Code() == ErrorCode::OutOfMemory);
        assert(v.Capacity() == 0);

        Result<void> pushRes = v.PushBack(1);
        assert(pushRes.HasError());
        assert(v.Size() == 0); // must not have written past an empty buffer
    }

    // --- Copy-assigning into a vector whose allocator fails: must not
    // crash or write out of bounds even though the allocation failed. ---
    {
        Vector<int> source;
        source.PushBack(1).Ignore();
        source.PushBack(2).Ignore();
        source.PushBack(3).Ignore();

        FailingAllocator failing;
        Vector<int> destination(failing);
        destination = source; // triggers a failing Reserve() internally

        // Must be safely bounded to whatever capacity was actually
        // secured (zero here), not a crash or garbage read/write.
        assert(destination.Capacity() == 0);
        assert(destination.Size() == 0);
    }

    // --- MakeUnique success + failure paths ---
    {
        Result<UniquePtr<Widget>> up = MakeUnique<Widget>(42);
        assert(up.HasValue());
        assert(up.Value()->x == 42);
        assert(Widget::alive == 1);
    }
    assert(Widget::alive == 0);

    {
        FailingAllocator failing;
        Allocator& failingRef = failing;
        Result<UniquePtr<Widget>> up = MakeUnique<Widget>(failingRef, 7);
        assert(up.HasError());
        assert(up.Error().Code() == ErrorCode::OutOfMemory);
    }

    printf("All tests passed.\n");
    return 0;
}