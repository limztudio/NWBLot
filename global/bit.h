// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compile.h"
#include "type.h"

#include <bit>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] constexpr NWB_INLINE T BitMask(const u32 bitIndex){
    return static_cast<T>(1) << bitIndex;
}

template<typename T>
    requires requires(T value){ std::countr_zero(value); }
[[nodiscard]] constexpr NWB_INLINE i32 CountTrailingZeros(const T value)noexcept{
    return std::countr_zero(value);
}

template<typename To, typename From>
    requires(sizeof(To) == sizeof(From) && IsTriviallyCopyable_V<To> && IsTriviallyCopyable_V<From>)
[[nodiscard]] constexpr NWB_INLINE To BitCast(const From& source)noexcept{
    return std::bit_cast<To>(source);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

