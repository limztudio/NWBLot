// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compile.h"
#include "type.h"

#include <bit>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] constexpr bool IsPowerOfTwo(const T value)noexcept(IsArithmetic_V<T>){
    return value > static_cast<T>(0) && (value & (value - static_cast<T>(1))) == static_cast<T>(0);
}

template<typename T>
[[nodiscard]] constexpr NWB_INLINE T FloorLog2(const T x)noexcept(IsArithmetic_V<T>){ return (x == 1) ? 0 : (1 + FloorLog2<T>(x >> 1)); }
template<typename T>
[[nodiscard]] constexpr NWB_INLINE T CeilLog2(const T x)noexcept(IsArithmetic_V<T>){ return (x == 1) ? 0 : (FloorLog2<T>(x - 1) + 1); }

template<typename T>
[[nodiscard]] constexpr NWB_INLINE T BitMask(const u32 bitIndex)noexcept(IsArithmetic_V<T>){
    return static_cast<T>(1) << bitIndex;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

