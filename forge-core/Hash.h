#pragma once

#include <cstdint>

#include "String.h"
#include "StringView.h"
#include "Types.h"

namespace forge::core
{

namespace detail
{

/// FNV-1a, 64-bit. Not cryptographic — just a fast, dependency-free,
/// well-distributed hash over an arbitrary byte range, used as the basis
/// for every "hash some bytes" specialization below.
[[nodiscard]]
inline u64 HashBytes(
    const char* data,
    Size size) noexcept
{
    u64 hash = 1469598103934665603ull; // FNV offset basis

    for (Size index = 0; index < size; ++index)
    {
        hash ^= static_cast<u8>(data[index]);
        hash *= 1099511628211ull; // FNV prime
    }

    return hash;
}

/// splitmix64's finalizer step. Used to avalanche plain integer keys
/// before they're masked down to a table index — without this,
/// sequential keys (0, 1, 2, 3, ...), which are extremely common in
/// practice, would land in sequential buckets under the power-of-two
/// table sizes HashMap/HashSet use, defeating the point of hashing them
/// at all.
[[nodiscard]]
constexpr u64 MixU64(
    u64 value) noexcept
{
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31;

    return value;
}

} // namespace detail

/// Hash trait used by HashMap/HashSet to turn a key into a u64. The
/// primary template is intentionally left undefined: a key type not
/// explicitly specialized here should fail to compile, not silently hash
/// by (say) its raw bytes including padding, or not compile with a
/// confusing template error deep inside HashMap's internals. Specialize
/// this for any additional key type you need to put in a HashMap/HashSet.
template<typename T>
struct Hash;

#define FORGE_CORE_DEFINE_INTEGRAL_HASH(type)                               \
    template<>                                                              \
    struct Hash<type>                                                       \
    {                                                                       \
        [[nodiscard]]                                                       \
        constexpr u64 operator()(type value) const noexcept                 \
        {                                                                   \
            return detail::MixU64(static_cast<u64>(value));                 \
        }                                                                   \
    }

FORGE_CORE_DEFINE_INTEGRAL_HASH(bool);
FORGE_CORE_DEFINE_INTEGRAL_HASH(char);
FORGE_CORE_DEFINE_INTEGRAL_HASH(i8);
FORGE_CORE_DEFINE_INTEGRAL_HASH(i16);
FORGE_CORE_DEFINE_INTEGRAL_HASH(i32);
FORGE_CORE_DEFINE_INTEGRAL_HASH(i64);
FORGE_CORE_DEFINE_INTEGRAL_HASH(u8);
FORGE_CORE_DEFINE_INTEGRAL_HASH(u16);
FORGE_CORE_DEFINE_INTEGRAL_HASH(u32);
FORGE_CORE_DEFINE_INTEGRAL_HASH(u64);

#undef FORGE_CORE_DEFINE_INTEGRAL_HASH

template<>
struct Hash<StringView>
{
    [[nodiscard]]
    u64 operator()(
        StringView value) const noexcept
    {
        return detail::HashBytes(value.Data(), value.Size());
    }
};

template<>
struct Hash<String>
{
    [[nodiscard]]
    u64 operator()(
        const String& value) const noexcept
    {
        return Hash<StringView>{}(value.View());
    }
};

/// Any pointer type. Mixes the address itself rather than hashing the
/// pointee — two HashMap<T*, V>s are keyed on identity, not value, same
/// as comparing two pointers with ==.
template<typename T>
struct Hash<T*>
{
    [[nodiscard]]
    u64 operator()(
        T* value) const noexcept
    {
        return detail::MixU64(
            static_cast<u64>(reinterpret_cast<std::uintptr_t>(value)));
    }
};

} // namespace forge::core
