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


inline constexpr i8 s_MinI8 = Limit<i8>::s_Min;
inline constexpr i8 s_MaxI8 = Limit<i8>::s_Max;

inline constexpr u8 s_MinU8 = Limit<u8>::s_Min;
inline constexpr u8 s_MaxU8 = Limit<u8>::s_Max;

inline constexpr i16 s_MinI16 = Limit<i16>::s_Min;
inline constexpr i16 s_MaxI16 = Limit<i16>::s_Max;

inline constexpr u16 s_MinU16 = Limit<u16>::s_Min;
inline constexpr u16 s_MaxU16 = Limit<u16>::s_Max;

inline constexpr i32 s_MinI32 = Limit<i32>::s_Min;
inline constexpr i32 s_MaxI32 = Limit<i32>::s_Max;

inline constexpr u32 s_MinU32 = Limit<u32>::s_Min;
inline constexpr u32 s_MaxU32 = Limit<u32>::s_Max;

inline constexpr i64 s_MinI64 = Limit<i64>::s_Min;
inline constexpr i64 s_MaxI64 = Limit<i64>::s_Max;

inline constexpr u64 s_MinU64 = Limit<u64>::s_Min;
inline constexpr u64 s_MaxU64 = Limit<u64>::s_Max;

inline constexpr isize s_MinIsize = Limit<isize>::s_Min;
inline constexpr isize s_MaxIsize = Limit<isize>::s_Max;

inline constexpr usize s_MinUsize = Limit<usize>::s_Min;
inline constexpr usize s_MaxUsize = Limit<usize>::s_Max;

inline constexpr f32 s_MinF32 = Limit<f32>::s_Min;
inline constexpr f32 s_MaxF32 = Limit<f32>::s_Max;

inline constexpr f64 s_MinF64 = Limit<f64>::s_Min;
inline constexpr f64 s_MaxF64 = Limit<f64>::s_Max;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] inline constexpr bool FitsU32(const T value){
    if constexpr(IsSigned_V<T>)
        return value >= T(0) && static_cast<u64>(value) <= static_cast<u64>(Limit<u32>::s_Max);
    else
        return static_cast<u64>(value) <= static_cast<u64>(Limit<u32>::s_Max);
}

template<typename T>
[[nodiscard]] inline constexpr bool CanRepresentU64(const u64 value){
    static_assert(IsArithmetic_V<T>, "CanRepresentU64 requires arithmetic target type");

    constexpr bool signedType = static_cast<T>(-1) < static_cast<T>(0);
    if constexpr(signedType){
        constexpr usize bitCount = sizeof(T) * 8u;
        constexpr u64 maxValue = bitCount >= 64u
            ? (Limit<u64>::s_Max >> 1u)
            : ((u64(1) << (bitCount - 1u)) - 1u)
        ;
        return value <= maxValue;
    }
    else{
        constexpr usize bitCount = sizeof(T) * 8u;
        constexpr u64 maxValue = bitCount >= 64u
            ? Limit<u64>::s_Max
            : ((u64(1) << bitCount) - 1u)
        ;
        return value <= maxValue;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

