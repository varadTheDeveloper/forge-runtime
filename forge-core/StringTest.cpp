// Standalone correctness test for Span<T>/StringView/String — same role
// as VectorSmokeTest.cpp/TimerSchedulerTest.cpp/IocpLoopTest.cpp: a
// throwaway verification harness, not part of the production moz.build
// build. Compiled and run directly under the real project constraints
// (C++17, exceptions disabled — see HISTORY.md/AGENTS.md), not the more
// permissive C++20-with-exceptions settings used before that mismatch
// was discovered.

#include "String.h"
#include "Span.h"

#include <cstdio>
#include <cstdlib>

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

//==============================================================================
// A deliberately-failing allocator, same role as Vector's own OOM tests —
// verifies String degrades safely (truncates) rather than crashing when
// an allocation fails, not just that the happy path works.
//==============================================================================

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

//==============================================================================
// Span<T>
//==============================================================================

void Test_Span()
{
    std::printf("Test_Span...\n");

    int values[5] = { 10, 20, 30, 40, 50 };
    Span<int> span(values, 5);

    CHECK(span.Size() == 5);
    CHECK(!span.Empty());
    CHECK(span[0] == 10);
    CHECK(span.Front() == 10);
    CHECK(span.Back() == 50);

    Span<int> middle = span.Subspan(1, 3);
    CHECK(middle.Size() == 3);
    CHECK(middle[0] == 20);
    CHECK(middle[2] == 40);

    // Over-large count clamps to what's actually available, doesn't read
    // out of bounds.
    Span<int> tail = span.Subspan(3, 1000);
    CHECK(tail.Size() == 2);
    CHECK(tail[0] == 40);
    CHECK(tail[1] == 50);

    // Offset at/beyond the end yields an empty span, not UB.
    Span<int> empty = span.Subspan(5, 10);
    CHECK(empty.Size() == 0);
    CHECK(empty.Empty());

    int sum = 0;
    for (int v : span)
    {
        sum += v;
    }
    CHECK(sum == 150);

    Span<int> defaultSpan;
    CHECK(defaultSpan.Empty());
    CHECK(defaultSpan.Data() == nullptr);
}

//==============================================================================
// StringView
//==============================================================================

void Test_StringView()
{
    std::printf("Test_StringView...\n");

    StringView empty;
    CHECK(empty.Empty());
    CHECK(empty.Size() == 0);

    StringView fromLiteral = "hello world";
    CHECK(fromLiteral.Size() == 11);
    CHECK(!fromLiteral.Empty());
    CHECK(fromLiteral[0] == 'h');

    CHECK(fromLiteral.StartsWith("hello"));
    CHECK(!fromLiteral.StartsWith("world"));
    CHECK(fromLiteral.EndsWith("world"));
    CHECK(!fromLiteral.EndsWith("hello"));

    // Empty prefix/suffix always matches, same as std::string_view.
    CHECK(fromLiteral.StartsWith(""));
    CHECK(fromLiteral.EndsWith(""));

    CHECK(fromLiteral.Find("world") == 6);
    CHECK(fromLiteral.Find("xyz") == StringView::kNotFound);
    CHECK(fromLiteral.Find("") == 0);
    CHECK(fromLiteral.Find("hello world") == 0);
    CHECK(fromLiteral.Find("hello world!") == StringView::kNotFound); // needle longer than haystack

    StringView sub = fromLiteral.Substr(6, 5);
    CHECK(sub == StringView("world"));
    CHECK(sub != fromLiteral);

    StringView tail = fromLiteral.Substr(6);
    CHECK(tail == StringView("world"));

    // Two views over different buffers with equal content compare equal.
    char buffer[] = "hello world";
    StringView fromBuffer(buffer, 11);
    CHECK(fromBuffer == fromLiteral);
    CHECK(fromBuffer.Data() != fromLiteral.Data()); // genuinely different buffers
}

//==============================================================================
// String — happy path
//==============================================================================

void Test_String_DefaultIsEmptyAndSafe()
{
    std::printf("Test_String_DefaultIsEmptyAndSafe...\n");

    String s;
    CHECK(s.Empty());
    CHECK(s.Size() == 0);
    CHECK(s.Capacity() == 0);
    CHECK(s.Data() == nullptr);

    // CStr() must be valid even though nothing was ever allocated.
    const char* c = s.CStr();
    CHECK(c != nullptr);
    CHECK(c[0] == '\0');
}

void Test_String_AppendCharGrowsCorrectly()
{
    std::printf("Test_String_AppendCharGrowsCorrectly...\n");

    String s;
    const char* word = "forge";

    for (int i = 0; word[i] != '\0'; ++i)
    {
        CHECK(!s.Append(word[i]).HasError());
    }

    CHECK(s.Size() == 5);
    CHECK(s.View() == StringView("forge"));
    CHECK(s.CStr()[5] == '\0'); // terminator maintained after every append
}

void Test_String_AppendStringView()
{
    std::printf("Test_String_AppendStringView...\n");

    String s;
    CHECK(!s.Append(StringView("hello, ")).HasError());
    CHECK(!s.Append(StringView("world")).HasError());
    CHECK(!s.Append('!').HasError());

    CHECK(s.Size() == 13);
    CHECK(s.View() == StringView("hello, world!"));
}

void Test_String_CreateFactory()
{
    std::printf("Test_String_CreateFactory...\n");

    Result<String> result = String::Create("created via factory");
    CHECK(result.HasValue());
    CHECK(result.Value().View() == StringView("created via factory"));
}

void Test_String_CopyIsIndependent()
{
    std::printf("Test_String_CopyIsIndependent...\n");

    String original;
    CHECK(!original.Append(StringView("original")).HasError());

    String copy(original);
    CHECK(copy.View() == StringView("original"));

    CHECK(!copy.Append(StringView("-modified")).HasError());
    CHECK(copy.View() == StringView("original-modified"));
    CHECK(original.View() == StringView("original")); // unaffected by mutating the copy
}

void Test_String_MoveStealsAndClearsSource()
{
    std::printf("Test_String_MoveStealsAndClearsSource...\n");

    String source;
    CHECK(!source.Append(StringView("move me")).HasError());

    String moved(std::move(source));
    CHECK(moved.View() == StringView("move me"));

    CHECK(source.Empty());
    CHECK(source.Data() == nullptr);
    CHECK(source.CStr()[0] == '\0'); // still safe to call after being moved from
}

void Test_String_CopyAndMoveAssignment()
{
    std::printf("Test_String_CopyAndMoveAssignment...\n");

    String a;
    CHECK(!a.Append(StringView("aaa")).HasError());

    String b;
    CHECK(!b.Append(StringView("bbb")).HasError());

    b = a;
    CHECK(b.View() == StringView("aaa"));
    CHECK(a.View() == StringView("aaa")); // a itself untouched by being assigned from

    String c;
    CHECK(!c.Append(StringView("ccc")).HasError());
    c = std::move(b);
    CHECK(c.View() == StringView("aaa"));
    CHECK(b.Empty());

    // Self-assignment must be a safe no-op, not a use-after-free. Routed
    // through a pointer/reference rather than writing `c = c;` directly,
    // since that literal syntax is flagged by clang's
    // -Wself-assign-overloaded — the aliasing behavior is exactly what
    // this test wants to exercise, so the warning is dodged rather than
    // suppressed away with a pragma.
    String* selfAlias = &c;
    c = *selfAlias;
    CHECK(c.View() == StringView("aaa"));
}

void Test_String_ComparisonsAcrossTypes()
{
    std::printf("Test_String_ComparisonsAcrossTypes...\n");

    Result<String> a = String::Create("same");
    Result<String> b = String::Create("same");
    Result<String> different = String::Create("different");

    CHECK(a.HasValue() && b.HasValue() && different.HasValue());

    CHECK(a.Value() == b.Value());        // String == String
    CHECK(a.Value() == StringView("same")); // String == StringView
    CHECK(a.Value() == "same");             // String == const char* (via StringView's implicit ctor)
    CHECK(!(a.Value() == different.Value()));
    CHECK(a.Value() != different.Value());
}

//==============================================================================
// String — allocation-failure (OOM) paths
//==============================================================================

void Test_String_ReserveReportsOomWithoutMutating()
{
    std::printf("Test_String_ReserveReportsOomWithoutMutating...\n");

    FailingAllocator allocator(/*failAfterNCalls=*/0); // fails immediately
    String s(allocator);

    Result<void> result = s.Reserve(64);
    CHECK(result.HasError());
    CHECK(s.Empty());
    CHECK(s.Capacity() == 0);
    CHECK(s.CStr()[0] == '\0'); // still safe/valid even though Reserve failed
}

void Test_String_AppendReportsOomWithoutCorruption()
{
    std::printf("Test_String_AppendReportsOomWithoutCorruption...\n");

    FailingAllocator allocator(/*failAfterNCalls=*/1); // first grow succeeds, next fails
    String s(allocator);

    CHECK(!s.Append('a').HasError()); // first allocation still allowed

    // Force enough appends to require a second, failing reallocation.
    bool sawFailure = false;
    for (int i = 0; i < 64; ++i)
    {
        if (s.Append('x').HasError())
        {
            sawFailure = true;
            break;
        }
    }

    CHECK(sawFailure);
    // Whatever the state, CStr() must still be valid/terminated — no
    // partial-write corruption from the failed append.
    CHECK(s.CStr()[s.Size()] == '\0');
}

void Test_String_CopyTruncatesSafelyOnOom()
{
    std::printf("Test_String_CopyTruncatesSafelyOnOom...\n");

    String original;
    CHECK(!original.Append(StringView("this will not fit after copy")).HasError());

    FailingAllocator allocator(/*failAfterNCalls=*/0); // the copy's own Reserve() will fail
    String copyAllocator(allocator);

    // Build the copy from `original` but routed through a failing
    // allocator by constructing directly with copy semantics: emulate via
    // assignment, since the copy constructor always uses the source's own
    // allocator (matching Vector<T>'s precedent) — assignment is the path
    // that can use *this*'s (failing) allocator against another string's
    // content.
    Result<void> reserveResult = copyAllocator.Reserve(1); // primes capacity_ tracking; still fails
    (void)reserveResult;

    copyAllocator = original; // copy-assignment: must not crash even though its allocator can't grow
    CHECK(copyAllocator.Empty()); // truncated to nothing, not a crash — same precedent as Vector<T>
    CHECK(copyAllocator.CStr()[0] == '\0');
}

} // namespace

int main()
{
    Test_Span();
    Test_StringView();
    Test_String_DefaultIsEmptyAndSafe();
    Test_String_AppendCharGrowsCorrectly();
    Test_String_AppendStringView();
    Test_String_CreateFactory();
    Test_String_CopyIsIndependent();
    Test_String_MoveStealsAndClearsSource();
    Test_String_CopyAndMoveAssignment();
    Test_String_ComparisonsAcrossTypes();
    Test_String_ReserveReportsOomWithoutMutating();
    Test_String_AppendReportsOomWithoutCorruption();
    Test_String_CopyTruncatesSafelyOnOom();

    if (g_failures == 0)
    {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }

    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
