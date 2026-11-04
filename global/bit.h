// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compile.h"
#include "type.h"

#include <bit>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] constexpr NWB_INLINE T FloorLog2(const T x)noexcept(IsArithmetic_V<T>){ return (x == 1) ? 0 : (1 + FloorLog2<T>(x >> 1)); }

template<typename T>
[[nodiscard]] constexpr NWB_INLINE T BitMask(const u32 bitIndex)noexcept(IsArithmetic_V<T>){
    return static_cast<T>(1) << bitIndex;
}

// A difference of exactly half the sequence space is unordered.
[[nodiscard]] inline constexpr b8 IsNewerSerialU32(const u32 value, const u32 previous)noexcept{
    const u32 difference = value - previous;
    return difference != 0u && difference < 0x80000000u;
}

// Equal values are admitted; a difference of half the sequence space is not.
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

// Alignment must be a nonzero power of two.
[[nodiscard]] inline constexpr usize AlignmentOffsetPowerOfTwo(const usize address, const usize alignment)noexcept{
    return (alignment - (address & (alignment - 1u))) & (alignment - 1u);
}

[[nodiscard]] inline constexpr u32 BitWidthLowU32(const u32 value, const u32 bitCount)noexcept{
    const u32 mask = bitCount >= 32u ? 0xffffffffu : ((1u << bitCount) - 1u);
    return static_cast<u32>(std::bit_width(value & mask));
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

// Packs the first two 32-bit Sobol dimensions into the low and high words.
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

template<typename T>
    requires requires(T value){ std::countr_zero(value); }
[[nodiscard]] constexpr NWB_INLINE i32 CountTrailingZeros(const T value)noexcept{
    return std::countr_zero(value);
}

template<typename T>
    requires requires(T value){ std::popcount(value); }
[[nodiscard]] constexpr NWB_INLINE i32 CountSetBits(const T value)noexcept{
    return std::popcount(value);
}

template<typename To, typename From>
    requires(sizeof(To) == sizeof(From) && IsTriviallyCopyable_V<To> && IsTriviallyCopyable_V<From>)
[[nodiscard]] constexpr NWB_INLINE To BitCast(const From& source)noexcept{
    return std::bit_cast<To>(source);
}

[[nodiscard]] inline constexpr b8 DifferentF64Bits(const f64 lhs, const f64 rhs)noexcept{
    return BitCast<u64>(lhs) != BitCast<u64>(rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

