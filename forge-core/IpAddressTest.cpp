// Standalone correctness test for IpAddress/Endpoint — pure logic, zero
// OS dependency, so this gets the full sandbox verification bar the same
// way PathTest.cpp does. Compiled under C++17/exceptions-disabled from
// the start, per the user's standing instruction to always target the
// real mach build's constraints.

#include "IpAddress.h"

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

String ToStringChecked(const IpAddress& address)
{
    Result<String> text = address.ToString();
    CHECK(text.HasValue());
    return std::move(text.Value());
}

void Test_IpAddress_V4ParseAndRoundTrip()
{
    std::printf("Test_IpAddress_V4ParseAndRoundTrip...\n");

    Result<IpAddress> parsed = IpAddress::Parse("192.168.1.1");
    CHECK(parsed.HasValue());
    CHECK(parsed.Value().IsV4());
    CHECK(parsed.Value() == IpAddress::V4(192, 168, 1, 1));
    CHECK(ToStringChecked(parsed.Value()).View() == StringView("192.168.1.1"));

    Result<IpAddress> zero = IpAddress::Parse("0.0.0.0");
    CHECK(zero.HasValue());
    CHECK(ToStringChecked(zero.Value()).View() == StringView("0.0.0.0"));

    Result<IpAddress> broadcast = IpAddress::Parse("255.255.255.255");
    CHECK(broadcast.HasValue());
    CHECK(broadcast.Value().Bytes().Size() == 4);
}

void Test_IpAddress_V4RejectsMalformedInput()
{
    std::printf("Test_IpAddress_V4RejectsMalformedInput...\n");

    CHECK(IpAddress::Parse("256.0.0.1").HasError());       // octet out of range
    CHECK(IpAddress::Parse("1.2.3").HasError());            // too few octets
    CHECK(IpAddress::Parse("1.2.3.4.5").HasError());        // too many octets
    CHECK(IpAddress::Parse("1.2.3.").HasError());            // trailing dot
    CHECK(IpAddress::Parse(".1.2.3").HasError());            // leading dot
    CHECK(IpAddress::Parse("1..2.3").HasError());            // empty octet
    CHECK(IpAddress::Parse("1.2.3.a").HasError());           // non-digit
    CHECK(IpAddress::Parse("").HasError());                  // empty
    CHECK(IpAddress::Parse("1.2.3.-1").HasError());          // negative

    // The leading-zero-octet rejection is a deliberate security choice
    // (see IpAddress.md's Non-Goals) — not a false negative.
    CHECK(IpAddress::Parse("010.0.0.1").HasError());
    CHECK(IpAddress::Parse("192.168.01.1").HasError());

    // A single "0" is fine — it's the multi-digit leading-zero case that's rejected.
    Result<IpAddress> zero = IpAddress::Parse("0.0.0.0");
    CHECK(zero.HasValue());
}

void Test_IpAddress_V6ParseAndRoundTrip()
{
    std::printf("Test_IpAddress_V6ParseAndRoundTrip...\n");

    Result<IpAddress> full = IpAddress::Parse("2001:db8:0:0:0:0:0:1");
    CHECK(full.HasValue());
    CHECK(full.Value().IsV6());
    CHECK(full.Value().Bytes().Size() == 16);
    // Canonical form compresses the longest zero run.
    CHECK(ToStringChecked(full.Value()).View() == StringView("2001:db8::1"));

    Result<IpAddress> loopback = IpAddress::Parse("::1");
    CHECK(loopback.HasValue());
    CHECK(ToStringChecked(loopback.Value()).View() == StringView("::1"));

    Result<IpAddress> unspecified = IpAddress::Parse("::");
    CHECK(unspecified.HasValue());
    CHECK(ToStringChecked(unspecified.Value()).View() == StringView("::"));

    Result<IpAddress> trailingCompression = IpAddress::Parse("1::");
    CHECK(trailingCompression.HasValue());
    CHECK(ToStringChecked(trailingCompression.Value()).View() == StringView("1::"));

    Result<IpAddress> noCompression = IpAddress::Parse("1:2:3:4:5:6:7:8");
    CHECK(noCompression.HasValue());
    CHECK(ToStringChecked(noCompression.Value()).View() == StringView("1:2:3:4:5:6:7:8"));

    // Uppercase input is accepted; canonical output is always lowercase.
    Result<IpAddress> upper = IpAddress::Parse("2001:DB8::1");
    CHECK(upper.HasValue());
    CHECK(ToStringChecked(upper.Value()).View() == StringView("2001:db8::1"));
    CHECK(upper.Value() == full.Value());

    // A lone zero group is never compressed (RFC 5952 requires a run of >= 2).
    Result<IpAddress> loneZero = IpAddress::Parse("1:0:3:4:5:6:7:8");
    CHECK(loneZero.HasValue());
    CHECK(ToStringChecked(loneZero.Value()).View() == StringView("1:0:3:4:5:6:7:8"));

    // Multiple equal-length zero runs: RFC 5952 says compress the FIRST one.
    Result<IpAddress> tiedRuns = IpAddress::Parse("1:0:0:2:0:0:3:4");
    CHECK(tiedRuns.HasValue());
    CHECK(ToStringChecked(tiedRuns.Value()).View() == StringView("1::2:0:0:3:4"));
}

void Test_IpAddress_V6RejectsMalformedInput()
{
    std::printf("Test_IpAddress_V6RejectsMalformedInput...\n");

    CHECK(IpAddress::Parse("1:2:3:4:5:6:7").HasError());        // too few groups, no "::"
    CHECK(IpAddress::Parse("1:2:3:4:5:6:7:8:9").HasError());    // too many groups
    CHECK(IpAddress::Parse("1::2::3").HasError());              // two "::"
    CHECK(IpAddress::Parse("1:::2").HasError());                 // triple colon
    CHECK(IpAddress::Parse("1:2:").HasError());                  // trailing single colon
    CHECK(IpAddress::Parse(":1:2").HasError());                  // leading single colon
    CHECK(IpAddress::Parse("1:2:3:4:5:6:7:gggg").HasError());   // non-hex group
    CHECK(IpAddress::Parse("1:2:3:4:5:6:7:88888").HasError());  // group too long
    CHECK(IpAddress::Parse("1:2:3:4:5:6:7::8").HasError());     // 7 groups + "::" leaves no room
}

void Test_IpAddress_V6ExplicitBytes()
{
    std::printf("Test_IpAddress_V6ExplicitBytes...\n");

    const u8 bytes[16] = {
        0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01,
    };

    IpAddress address = IpAddress::V6(Span<const u8>(bytes, 16));
    CHECK(address.IsV6());
    CHECK(ToStringChecked(address).View() == StringView("2001:db8::1"));
}

void Test_Endpoint_ParseV4()
{
    std::printf("Test_Endpoint_ParseV4...\n");

    Result<Endpoint> endpoint = Endpoint::Parse("192.168.1.1:8080");
    CHECK(endpoint.HasValue());
    CHECK(endpoint.Value().Address() == IpAddress::V4(192, 168, 1, 1));
    CHECK(endpoint.Value().Port() == 8080);

    Result<String> text = endpoint.Value().ToString();
    CHECK(text.HasValue());
    CHECK(text.Value().View() == StringView("192.168.1.1:8080"));
}

void Test_Endpoint_ParseV6RequiresBrackets()
{
    std::printf("Test_Endpoint_ParseV6RequiresBrackets...\n");

    Result<Endpoint> bracketed = Endpoint::Parse("[::1]:8080");
    CHECK(bracketed.HasValue());
    CHECK(bracketed.Value().Address().IsV6());
    CHECK(bracketed.Value().Port() == 8080);

    Result<String> text = bracketed.Value().ToString();
    CHECK(text.HasValue());
    CHECK(text.Value().View() == StringView("[::1]:8080"));

    // An unbracketed V6 literal is ambiguous with the port separator and
    // must be rejected, not guessed at.
    CHECK(Endpoint::Parse("::1:8080").HasError());
}

void Test_Endpoint_ParseRejectsMalformedInput()
{
    std::printf("Test_Endpoint_ParseRejectsMalformedInput...\n");

    CHECK(Endpoint::Parse("").HasError());
    CHECK(Endpoint::Parse("192.168.1.1").HasError());          // no port
    CHECK(Endpoint::Parse("192.168.1.1:").HasError());          // empty port
    CHECK(Endpoint::Parse("192.168.1.1:70000").HasError());     // port out of range
    CHECK(Endpoint::Parse("192.168.1.1:abc").HasError());       // non-numeric port
    CHECK(Endpoint::Parse("192.168.1.1:080").HasError());       // leading-zero port
    CHECK(Endpoint::Parse("[::1]8080").HasError());              // missing ':' after ']'
    CHECK(Endpoint::Parse("[::1").HasError());                   // unterminated bracket

    // Port 0 is a single digit, not a leading-zero multi-digit case, and
    // is a legal (if unusual) port value.
    Result<Endpoint> zeroPort = Endpoint::Parse("192.168.1.1:0");
    CHECK(zeroPort.HasValue());
    CHECK(zeroPort.Value().Port() == 0);
}

void Test_IpAddress_ToStringReportsOomWithoutCorruption()
{
    std::printf("Test_IpAddress_ToStringReportsOomWithoutCorruption...\n");

    FailingAllocator allocator(/*failAfterNCalls=*/0); // fails immediately
    IpAddress address = IpAddress::V4(1, 2, 3, 4);

    Result<String> text = address.ToString(allocator);
    CHECK(text.HasError());
    CHECK(text.Error() == ErrorCode::OutOfMemory);
}

} // namespace

int main()
{
    Test_IpAddress_V4ParseAndRoundTrip();
    Test_IpAddress_V4RejectsMalformedInput();
    Test_IpAddress_V6ParseAndRoundTrip();
    Test_IpAddress_V6RejectsMalformedInput();
    Test_IpAddress_V6ExplicitBytes();
    Test_Endpoint_ParseV4();
    Test_Endpoint_ParseV6RequiresBrackets();
    Test_Endpoint_ParseRejectsMalformedInput();
    Test_IpAddress_ToStringReportsOomWithoutCorruption();

    if (g_failures == 0)
    {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }

    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
