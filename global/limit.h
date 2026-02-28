// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <limits>

#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
struct Limit{
    static constexpr T s_Min = (std::numeric_limits<T>::lowest)();
    static constexpr T s_Max = (std::numeric_limits<T>::max)();
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr i8 s_MinI8 = Limit<i8>::s_Min;
static constexpr i8 s_MaxI8 = Limit<i8>::s_Max;

static constexpr u8 s_MinU8 = Limit<u8>::s_Min;
static constexpr u8 s_MaxU8 = Limit<u8>::s_Max;

static constexpr i16 s_MinI16 = Limit<i16>::s_Min;
static constexpr i16 s_MaxI16 = Limit<i16>::s_Max;

static constexpr u16 s_MinU16 = Limit<u16>::s_Min;
static constexpr u16 s_MaxU16 = Limit<u16>::s_Max;

static constexpr i32 s_MinI32 = Limit<i32>::s_Min;
static constexpr i32 s_MaxI32 = Limit<i32>::s_Max;

static constexpr u32 s_MinU32 = Limit<u32>::s_Min;
static constexpr u32 s_MaxU32 = Limit<u32>::s_Max;

static constexpr i64 s_MinI64 = Limit<i64>::s_Min;
static constexpr i64 s_MaxI64 = Limit<i64>::s_Max;

static constexpr u64 s_MinU64 = Limit<u64>::s_Min;
static constexpr u64 s_MaxU64 = Limit<u64>::s_Max;

static constexpr isize s_MinIsize = Limit<isize>::s_Min;
static constexpr isize s_MaxIsize = Limit<isize>::s_Max;

static constexpr usize s_MinUsize = Limit<usize>::s_Min;
static constexpr usize s_MaxUsize = Limit<usize>::s_Max;

static constexpr f32 s_MinF32 = Limit<f32>::s_Min;
static constexpr f32 s_MaxF32 = Limit<f32>::s_Max;

static constexpr f64 s_MinF64 = Limit<f64>::s_Min;
static constexpr f64 s_MaxF64 = Limit<f64>::s_Max;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

