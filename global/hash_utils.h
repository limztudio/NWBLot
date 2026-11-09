// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compile.h"
#include "limit.h"
#include "span.h"
#include "text_utils.h"
#include "type.h"
#include "type_borrow.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_Fnv32OffsetBasis = 2166136261u;
inline constexpr u32 s_Fnv32Prime = 16777619u;
inline constexpr u64 s_Fnv64OffsetBasis = 14695981039346656037ull;
inline constexpr u64 s_Fnv64Prime = 1099511628211ull;
inline constexpr usize s_HashCombineGoldenRatio = 0x9e3779b9u;
inline constexpr u64 s_HashCombineGoldenRatio64 = 0x9e3779b97f4a7c15ull;
inline constexpr usize s_HashCombineLeftShift = 6u;
inline constexpr usize s_HashCombineRightShift = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline constexpr u64 MixMurmur64Finalizer(u64 value)noexcept{
    value ^= value >> 33u;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33u;
    value *= 0xc4ceb9fe1a85ec53ull;
    value ^= value >> 33u;
    return value;
}

[[nodiscard]] inline constexpr u32 UpdateFnv32Byte(const u32 hash, const u8 byte)noexcept{
    return (hash ^ byte) * s_Fnv32Prime;
}

[[nodiscard]] inline constexpr u32 ComputeFnv32TextHash(const AStringView text)noexcept{
    u32 hash = s_Fnv32OffsetBasis;
    for(const char value : text)
        hash = UpdateFnv32Byte(hash, static_cast<u8>(value));
    return hash;
}

[[nodiscard]] inline constexpr u64 UpdateFnv64Byte(const u64 hash, const u8 byte)noexcept{
    return (hash ^ byte) * s_Fnv64Prime;
}

[[nodiscard]] inline constexpr u64 UpdateFnv64U32LittleEndian(u64 hash, const u32 value)noexcept{
    for(usize index = 0u; index < sizeof(value); ++index)
        hash = UpdateFnv64Byte(hash, static_cast<u8>(value >> (index * s_BitsPerByte)));
    return hash;
}

[[nodiscard]] inline constexpr u64 UpdateFnv64U64LittleEndian(u64 hash, const u64 value)noexcept{
    for(usize index = 0u; index < sizeof(value); ++index)
        hash = UpdateFnv64Byte(hash, static_cast<u8>(value >> (index * s_BitsPerByte)));
    return hash;
}

[[nodiscard]] inline constexpr u64 ComputeFnv64AsciiLowerText(const AStringView text)noexcept{
    u64 hash = s_Fnv64OffsetBasis;
    for(const char ch : text)
        hash = UpdateFnv64Byte(hash, ToAsciiLowerByte(static_cast<u8>(ch)));
    return hash;
}

[[nodiscard]] inline u64 UpdateFnv64(u64 hash, const Span<const u8> bytes)noexcept{
    if(bytes.data() == nullptr || bytes.empty())
        return hash;

    for(const u8 byte : bytes)
        hash = UpdateFnv64Byte(hash, byte);

    return hash;
}

template<typename T>
inline void Fnv64AppendValue(u64& inOutHash, const T& value)noexcept(noexcept(&value)){
    static_assert(IsTriviallyCopyable_V<T>, "Fnv64AppendValue requires a trivially copyable value");
    inOutHash = UpdateFnv64(inOutHash, Span<const u8>(reinterpret_cast<const u8*>(&value), sizeof(value)));
}

inline void Fnv64AppendBuffer(u64& inOutHash, const Span<const u8> bytes)noexcept{
    const usize byteCount = bytes.size();
    Fnv64AppendValue(inOutHash, byteCount);
    inOutHash = UpdateFnv64(inOutHash, bytes);
}

inline void Fnv64AppendBool(u64& inOutHash, const b8 value)noexcept{
    const u8 byteValue = value ? 1u : 0u;
    Fnv64AppendValue(inOutHash, byteValue);
}

template<typename CharT>
[[nodiscard]] inline constexpr u64 UpdateFnv64TextCanonical(u64 hash, const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    for(const CharT ch : text){
        hash ^= static_cast<u64>(static_cast<u8>(Canonicalize(ch)));
        hash *= s_Fnv64Prime;
    }

    return hash;
}

template<typename CharT>
[[nodiscard]] inline u64 UpdateFnv64TextExact(u64 hash, const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return UpdateFnv64(hash, Span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size() * sizeof(CharT)));
}

[[nodiscard]] inline u64 ComputeFnv64Bytes(const void* data, const usize byteCount)noexcept{
    return UpdateFnv64(s_Fnv64OffsetBasis, Span<const u8>(static_cast<const u8*>(data), byteCount));
}

template<typename T>
    requires(IsSame_V<T, u32> || IsSame_V<T, u64>)
inline constexpr void HashCombineHash(T& seed, const T hash, const T goldenRatio)noexcept{
    seed ^= hash + goldenRatio + (seed << s_HashCombineLeftShift) + (seed >> s_HashCombineRightShift);
}

inline constexpr void HashCombineHash(usize& seed, const usize hash)noexcept{
    HashCombineHash(seed, hash, s_HashCombineGoldenRatio);
}

template<typename T>
[[nodiscard]] inline usize HashValue(const T& value)noexcept(noexcept(Hasher<T>{}(value))){
    return Hasher<T>{}(value);
}

template<typename T>
inline void HashCombine(usize& seed, const T& value)noexcept(noexcept(HashCombineHash(seed, Hasher<T>{}(value)))){
    HashCombineHash(seed, Hasher<T>{}(value));
}

template<typename T>
[[nodiscard]] inline usize HashCombineValue(usize seed, const T& value)noexcept(noexcept(HashCombine(seed, value))){
    HashCombine(seed, value);
    return seed;
}

template<typename T>
    requires(IsSame_V<T, f32> || IsSame_V<T, f64>)
[[nodiscard]] inline auto FloatingHashBits(T value)noexcept{
    using Bits = Conditional_T<IsSame_V<T, f32>, u32, u64>;
    if(value == T{ 0 })
        value = T{ 0 };

    Bits bits = 0u;
    FOUNDATION_MEMCPY(&bits, sizeof(bits), &value, sizeof(value));
    return bits;
}

template<typename T>
    requires(IsSame_V<T, f32> || IsSame_V<T, f64>)
inline void HashCombineFloating(usize& seed, const T value)noexcept{
    HashCombine(seed, FloatingHashBits(value));
}

template<typename CharT>
[[nodiscard]] inline u64 ComputeFnv64Text(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return ComputeFnv64Bytes(text.data(), text.size() * sizeof(CharT));
}

template<StringViewConvertible StringT>
[[nodiscard]] inline u64 ComputeFnv64Text(const StringT& text)noexcept(noexcept(AsBasicStringView(text)) && IsArithmetic_V<typename StringT::value_type>){
    return ComputeFnv64Text(AsBasicStringView(text));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

