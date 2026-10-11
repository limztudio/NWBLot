// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../assert.h"
#include "../simplemath.h"
#include "macro.h"
#include "constant_double.h"
#include "convert_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Active component masks for vector helpers that consume only a subset of SIMD lanes.
namespace VectorDoubleComponentMask{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_DoubleXY = 0x3u;
inline constexpr u32 s_DoubleXYZ = 0x7u;
inline constexpr u32 s_DoubleXYZW = 0xFu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using UIntVector = u64 __attribute__((ext_vector_type(4)));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
NWB_INLINE T* StridePointer(T* pointer, usize stride, usize index)noexcept{
    return reinterpret_cast<T*>(reinterpret_cast<u8*>(pointer) + stride * index);
}

template<typename T>
NWB_INLINE const T* StridePointer(const T* pointer, usize stride, usize index)noexcept{
    return reinterpret_cast<const T*>(reinterpret_cast<const u8*>(pointer) + stride * index);
}

NWB_INLINE u32 ComparisonMaskR(u32 mask, u32 activeMask)noexcept{
    mask &= activeMask;
    if(mask == activeMask)
        return s_DoubleCRMASK_CR6TRUE;
    if(mask == 0)
        return s_DoubleCRMASK_CR6FALSE;
    return 0;
}

NWB_INLINE u32 BoundsMaskR(u32 mask, u32 activeMask)noexcept{
    return (mask & activeMask) == activeMask ? s_DoubleCRMASK_CR6BOUNDS : 0;
}

template<u32 Lane>
NWB_INLINE f64 NWB_SIMD_CALL GetLane(SIMDVectorDouble value)noexcept{
    static_assert(Lane < 4u);
    return value[Lane];
}

template<u32 Lane>
NWB_INLINE u64 NWB_SIMD_CALL GetIntLane(SIMDVectorDouble value)noexcept{
    static_assert(Lane < 4u);
    return BitCast<u64>(value[Lane]);
}

template<u32 Lane>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SplatLane(SIMDVectorDouble value)noexcept{
    static_assert(Lane < 4u);
    return __builtin_shufflevector(value, value, Lane, Lane, Lane, Lane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

