// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compile.h"
#include "type.h"

#include <bit>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] constexpr FOUNDATION_INLINE T FloorLog2(const T x)noexcept(IsArithmetic_V<T>){ return (x == 1) ? 0 : (1 + FloorLog2<T>(x >> 1)); }

template<typename T>
[[nodiscard]] constexpr FOUNDATION_INLINE T BitMask(const u32 bitIndex)noexcept(IsArithmetic_V<T>){
    return static_cast<T>(1) << bitIndex;
}

[[nodiscard]] inline constexpr b8 IsNewerSerialU32(const u32 value, const u32 previous)noexcept{
    const u32 difference = value - previous;
    return difference != 0u && difference < 0x80000000u;
}

[[nodiscard]] inline constexpr b8 IsNotOlderSerialU32(const u32 value, const u32 previous)noexcept{
    return value - previous < 0x80000000u;
}

[[nodiscard]] inline constexpr b8 IsSerialU32InWindow(const u32 value, const u32 first, const u32 last)noexcept{
    const u32 distance = value - first;
    const u32 lastDistance = last - first;
    return distance < 0x80000000u && lastDistance < 0x80000000u && distance <= lastDistance;
}

template<typename T>
    requires(IsIntegral_V<T> && !IsSigned_V<T> && !IsSame_V<T, b8>)
[[nodiscard]] inline constexpr b8 HasSingleSetBit(const T value)noexcept{
    return std::has_single_bit(value);
}

template<typename T>
    requires(IsIntegral_V<T> && !IsSigned_V<T> && !IsSame_V<T, b8>)
[[nodiscard]] inline constexpr T LowestSetBit(const T value)noexcept{
    return static_cast<T>(value & static_cast<T>(0u - value));
}

[[nodiscard]] inline constexpr usize AlignmentOffsetPowerOfTwo(const usize address, const usize alignment)noexcept{
    return (alignment - (address & (alignment - 1u))) & (alignment - 1u);
}

template<typename T>
    requires requires(T value){ std::countl_zero(value); }
[[nodiscard]] constexpr FOUNDATION_INLINE i32 CountLeadingZeros(const T value)noexcept{
    constexpr i32 s_BitWidth = static_cast<i32>(sizeof(T) * 8u);
#if __has_builtin(__builtin_clzg)
    return __builtin_clzg(value, s_BitWidth);
#else
    if(value == static_cast<T>(0))
        return s_BitWidth;
    if constexpr(sizeof(T) <= sizeof(unsigned int))
        return __builtin_clz(static_cast<unsigned int>(value)) - (static_cast<i32>(sizeof(unsigned int) * 8u) - s_BitWidth);
    else if constexpr(sizeof(T) <= sizeof(unsigned long))
        return __builtin_clzl(static_cast<unsigned long>(value)) - (static_cast<i32>(sizeof(unsigned long) * 8u) - s_BitWidth);
    else if constexpr(sizeof(T) <= sizeof(unsigned long long))
        return __builtin_clzll(static_cast<unsigned long long>(value)) - (static_cast<i32>(sizeof(unsigned long long) * 8u) - s_BitWidth);
    else
        return std::countl_zero(value);
#endif
}

template<typename T>
    requires requires(T value){ std::bit_width(value); }
[[nodiscard]] constexpr FOUNDATION_INLINE i32 BitWidth(const T value)noexcept{
    return static_cast<i32>(sizeof(T) * 8u) - CountLeadingZeros(value);
}

template<typename T>
    requires requires(T value){ std::countr_zero(value); }
[[nodiscard]] constexpr FOUNDATION_INLINE i32 CountTrailingZeros(const T value)noexcept{
    constexpr i32 s_BitWidth = static_cast<i32>(sizeof(T) * 8u);
#if __has_builtin(__builtin_ctzg)
    return __builtin_ctzg(value, s_BitWidth);
#else
    if(value == static_cast<T>(0))
        return s_BitWidth;
    if constexpr(sizeof(T) <= sizeof(unsigned int))
        return __builtin_ctz(static_cast<unsigned int>(value));
    else if constexpr(sizeof(T) <= sizeof(unsigned long))
        return __builtin_ctzl(static_cast<unsigned long>(value));
    else if constexpr(sizeof(T) <= sizeof(unsigned long long))
        return __builtin_ctzll(static_cast<unsigned long long>(value));
    else
        return std::countr_zero(value);
#endif
}

template<typename T>
    requires requires(T value){ std::popcount(value); }
[[nodiscard]] constexpr FOUNDATION_INLINE i32 CountSetBits(const T value)noexcept{
#if __has_builtin(__builtin_popcountg)
    return __builtin_popcountg(value);
#else
    if constexpr(sizeof(T) <= sizeof(unsigned int))
        return __builtin_popcount(static_cast<unsigned int>(value));
    else if constexpr(sizeof(T) <= sizeof(unsigned long))
        return __builtin_popcountl(static_cast<unsigned long>(value));
    else if constexpr(sizeof(T) <= sizeof(unsigned long long))
        return __builtin_popcountll(static_cast<unsigned long long>(value));
    else
        return std::popcount(value);
#endif
}

[[nodiscard]] inline constexpr u32 BitWidthLowU32(const u32 value, const u32 bitCount)noexcept{
    const u32 mask = bitCount >= 32u ? 0xffffffffu : ((1u << bitCount) - 1u);
    return static_cast<u32>(BitWidth(value & mask));
}

[[nodiscard]] inline constexpr u64 PackU32Words(const u32 low, const u32 high)noexcept{
    return static_cast<u64>(low) | (static_cast<u64>(high) << 32u);
}

[[nodiscard]] inline constexpr u32 LowU32Word(const u64 value)noexcept{
    return static_cast<u32>(value);
}

[[nodiscard]] inline constexpr u32 HighU32Word(const u64 value)noexcept{
    return static_cast<u32>(value >> 32u);
}

[[nodiscard]] inline constexpr u32 PackFourCharTag(const char a, const char b, const char c, const char d)noexcept{
    return (static_cast<u32>(a) << 24u) | (static_cast<u32>(b) << 16u) | (static_cast<u32>(c) << 8u) | static_cast<u32>(d);
}

[[nodiscard]] inline constexpr u64 PackUnorderedU32Pair(const u32 first, const u32 second)noexcept{
    const u32 low = first < second ? first : second;
    const u32 high = first < second ? second : first;
    return (static_cast<u64>(low) << 32u) | static_cast<u64>(high);
}

[[nodiscard]] inline constexpr u64 ComputeSobol2U32(u32 index)noexcept{
    u32 x = 0u;
    u32 y = 0u;
    u32 direction = 0x80000000u;
    for(u32 bit = 0u; bit < 32u; ++bit){
        const u32 selected = index & 1u;
        x = (x << 1u) | selected;
        if(selected != 0u)
            y ^= direction;
        index >>= 1u;
        direction ^= direction >> 1u;
    }
    return PackU32Words(x, y);
}

template<typename To, typename From>
    requires(sizeof(To) == sizeof(From) && IsTriviallyCopyable_V<To> && IsTriviallyCopyable_V<From>)
[[nodiscard]] constexpr FOUNDATION_INLINE To BitCast(const From& source)noexcept{
    return std::bit_cast<To>(source);
}

[[nodiscard]] inline constexpr b8 DifferentF64Bits(const f64 lhs, const f64 rhs)noexcept{
    return BitCast<u64>(lhs) != BitCast<u64>(rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

