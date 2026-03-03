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

