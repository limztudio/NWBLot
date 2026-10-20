// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compile.h"
#include "limit.h"
#include "text_utils.h"
#include "type.h"
#include "type_borrow.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u64 s_Fnv64OffsetBasis = 14695981039346656037ull;
inline constexpr u64 s_Fnv64Prime = 1099511628211ull;
inline constexpr usize s_HashCombineGoldenRatio = 0x9e3779b9u;
inline constexpr u64 s_HashCombineGoldenRatio64 = 0x9e3779b97f4a7c15ull;
inline constexpr usize s_HashCombineLeftShift = 6u;
inline constexpr usize s_HashCombineRightShift = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline constexpr u64 UpdateFnv64Byte(const u64 hash, const u8 byte)noexcept{
    return (hash ^ byte) * s_Fnv64Prime;
}

[[nodiscard]] inline constexpr u64 UpdateFnv64U64LittleEndian(u64 hash, const u64 value)noexcept{
    for(usize index = 0u; index < sizeof(value); ++index)
        hash = UpdateFnv64Byte(hash, static_cast<u8>(value >> (index * s_BitsPerByte)));
    return hash;
}

[[nodiscard]] inline u64 UpdateFnv64(u64 hash, const u8* bytes, const usize byteCount)noexcept{
    if(bytes == nullptr || byteCount == 0)
        return hash;

    for(usize i = 0u; i < byteCount; ++i){
        hash = UpdateFnv64Byte(hash, bytes[i]);
    }

    return hash;
}

template<typename T>
inline void Fnv64AppendValue(u64& inOutHash, const T& value)noexcept(noexcept(&value)){
    static_assert(IsTriviallyCopyable_V<T>, "Fnv64AppendValue requires a trivially copyable value");
    inOutHash = UpdateFnv64(inOutHash, reinterpret_cast<const u8*>(&value), sizeof(value));
}

inline void Fnv64AppendBuffer(u64& inOutHash, const u8* bytes, const usize byteCount)noexcept{
    Fnv64AppendValue(inOutHash, byteCount);
    inOutHash = UpdateFnv64(inOutHash, bytes, byteCount);
}

inline void Fnv64AppendBool(u64& inOutHash, const bool value)noexcept{
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
    return UpdateFnv64(hash, reinterpret_cast<const u8*>(text.data()), text.size() * sizeof(CharT));
}

[[nodiscard]] inline u64 ComputeFnv64Bytes(const void* data, const usize byteCount)noexcept{
    return UpdateFnv64(s_Fnv64OffsetBasis, static_cast<const u8*>(data), byteCount);
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
inline void HashCombine(usize& seed, const T& value)noexcept(noexcept(HashCombineHash(seed, Hasher<T>{}(value)))){
    HashCombineHash(seed, Hasher<T>{}(value));
}

template<typename T>
    requires(IsSame_V<T, f32> || IsSame_V<T, f64>)
[[nodiscard]] inline auto FloatingHashBits(T value)noexcept{
    using Bits = Conditional_T<IsSame_V<T, f32>, u32, u64>;
    if(value == T{ 0 })
        value = T{ 0 };

    Bits bits = 0u;
    NWB_MEMCPY(&bits, sizeof(bits), &value, sizeof(value));
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
template<typename CharT, typename ArenaT>
[[nodiscard]] inline u64 ComputeFnv64Text(const BasicString<CharT, ArenaT>& text)noexcept(IsArithmetic_V<CharT>){
    return ComputeFnv64Text(BasicStringView<CharT>(text));
}
template<typename StringT>
    requires requires(const StringT& text){
        typename StringT::value_type;
        text.data();
        text.size();
    }
[[nodiscard]] inline u64 ComputeFnv64Text(const StringT& text)noexcept(noexcept(BasicStringView<typename StringT::value_type>(text.data(), text.size())) && IsArithmetic_V<typename StringT::value_type>){
    using CharT = typename StringT::value_type;
    return ComputeFnv64Text(BasicStringView<CharT>(text.data(), text.size()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

