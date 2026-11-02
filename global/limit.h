// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <limits>

#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
struct Limit{
    static inline constexpr T s_Min = (std::numeric_limits<T>::lowest)();
    static inline constexpr T s_Max = (std::numeric_limits<T>::max)();
    static inline constexpr T s_Infinity = (std::numeric_limits<T>::infinity)();
    static inline constexpr T s_QuietNaN = (std::numeric_limits<T>::quiet_NaN)();
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr i16 s_MinI16 = Limit<i16>::s_Min;
inline constexpr i16 s_MaxI16 = Limit<i16>::s_Max;

inline constexpr u16 s_MaxU16 = Limit<u16>::s_Max;

inline constexpr i32 s_MaxI32 = Limit<i32>::s_Max;

inline constexpr u32 s_MaxU32 = Limit<u32>::s_Max;

inline constexpr f32 s_MaxF32 = Limit<f32>::s_Max;

inline constexpr usize s_BitsPerByte = 8u;
inline constexpr usize s_BitsPerU64 = 64u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] inline constexpr b8 FitsU32(const T value)noexcept(IsArithmetic_V<T>){
    if constexpr(IsSigned_V<T>)
        return value >= T(0) && static_cast<u64>(value) <= static_cast<u64>(Limit<u32>::s_Max);
    else
        return static_cast<u64>(value) <= static_cast<u64>(Limit<u32>::s_Max);
}

template<typename T>
[[nodiscard]] inline constexpr b8 CanRepresentU64(const u64 value)noexcept{
    static_assert(IsArithmetic_V<T>, "CanRepresentU64 requires arithmetic target type");

    constexpr b8 s_SignedType = static_cast<T>(-1) < static_cast<T>(0);
    if constexpr(s_SignedType){
        constexpr usize s_BitCount = sizeof(T) * s_BitsPerByte;
        constexpr u64 s_MaxValue = s_BitCount >= s_BitsPerU64 ? (Limit<u64>::s_Max >> 1u) : ((u64(1) << (s_BitCount - 1u)) - 1u);
        return value <= s_MaxValue;
    }
    else{
        constexpr usize s_BitCount = sizeof(T) * s_BitsPerByte;
        constexpr u64 s_MaxValue = s_BitCount >= s_BitsPerU64 ? Limit<u64>::s_Max : ((u64(1) << s_BitCount) - 1u);
        return value <= s_MaxValue;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

