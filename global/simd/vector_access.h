// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_construct.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE f32 FOUNDATION_SIMD_CALL VectorGetX(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetLane<0u>(value); }

FOUNDATION_INLINE f64 FOUNDATION_SIMD_CALL VectorDoubleGetX(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<0u>(value); }

FOUNDATION_INLINE f32 FOUNDATION_SIMD_CALL VectorGetY(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetLane<1u>(value); }

FOUNDATION_INLINE f64 FOUNDATION_SIMD_CALL VectorDoubleGetY(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<1u>(value); }

FOUNDATION_INLINE f32 FOUNDATION_SIMD_CALL VectorGetZ(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetLane<2u>(value); }

FOUNDATION_INLINE f64 FOUNDATION_SIMD_CALL VectorDoubleGetZ(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<2u>(value); }

FOUNDATION_INLINE f32 FOUNDATION_SIMD_CALL VectorGetW(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetLane<3u>(value); }

FOUNDATION_INLINE f64 FOUNDATION_SIMD_CALL VectorDoubleGetW(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<3u>(value); }

FOUNDATION_INLINE f32 FOUNDATION_SIMD_CALL VectorGetByIndex(SIMDVectorSingle value, usize index)noexcept{
    FOUNDATION_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorGetX(value);
    case 1u: return VectorGetY(value);
    case 2u: return VectorGetZ(value);
    default: return VectorGetW(value);
    }
}

FOUNDATION_INLINE f64 FOUNDATION_SIMD_CALL VectorDoubleGetByIndex(SIMDVectorDouble value, usize index)noexcept{
    FOUNDATION_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleGetX(value);
    case 1u: return VectorDoubleGetY(value);
    case 2u: return VectorDoubleGetZ(value);
    default: return VectorDoubleGetW(value);
    }
}

FOUNDATION_INLINE u32 FOUNDATION_SIMD_CALL VectorGetIntX(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetIntLane<0u>(value); }

FOUNDATION_INLINE u64 FOUNDATION_SIMD_CALL VectorDoubleGetIntX(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<0u>(value); }

FOUNDATION_INLINE u32 FOUNDATION_SIMD_CALL VectorGetIntY(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetIntLane<1u>(value); }

FOUNDATION_INLINE u64 FOUNDATION_SIMD_CALL VectorDoubleGetIntY(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<1u>(value); }

FOUNDATION_INLINE u32 FOUNDATION_SIMD_CALL VectorGetIntZ(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetIntLane<2u>(value); }

FOUNDATION_INLINE u64 FOUNDATION_SIMD_CALL VectorDoubleGetIntZ(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<2u>(value); }

FOUNDATION_INLINE u32 FOUNDATION_SIMD_CALL VectorGetIntW(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetIntLane<3u>(value); }

FOUNDATION_INLINE u64 FOUNDATION_SIMD_CALL VectorDoubleGetIntW(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<3u>(value); }

FOUNDATION_INLINE u32 FOUNDATION_SIMD_CALL VectorGetIntByIndex(SIMDVectorSingle value, usize index)noexcept{
    FOUNDATION_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorGetIntX(value);
    case 1u: return VectorGetIntY(value);
    case 2u: return VectorGetIntZ(value);
    default: return VectorGetIntW(value);
    }
}

FOUNDATION_INLINE u64 FOUNDATION_SIMD_CALL VectorDoubleGetIntByIndex(SIMDVectorDouble value, usize index)noexcept{
    FOUNDATION_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleGetIntX(value);
    case 1u: return VectorDoubleGetIntY(value);
    case 2u: return VectorDoubleGetIntZ(value);
    default: return VectorDoubleGetIntW(value);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL VectorSetX(SIMDVectorSingle value, f32 x)noexcept{
#if defined(FOUNDATION_HAS_SCALAR)
    value.f[0] = x;
    return value;
#elif defined(FOUNDATION_HAS_NEON)
    return vsetq_lane_f32(x, value, 0);
#else
    return _mm_move_ss(value, _mm_set_ss(x));
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDoubleSetX(SIMDVectorDouble value, f64 x)noexcept{
    return SIMDVectorDoubleDetail::SetLane<0u>(value, x);
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL VectorSetY(SIMDVectorSingle value, f32 y)noexcept{
#if defined(FOUNDATION_HAS_SCALAR)
    value.f[1] = y;
    return value;
#elif defined(FOUNDATION_HAS_NEON)
    return vsetq_lane_f32(y, value, 1);
#elif defined(FOUNDATION_HAS_SSE4)
    return _mm_insert_ps(value, _mm_set_ss(y), s_SIMDSse4InsertLaneYMask);
#else
    return SIMDVectorDetail::SetLane<1u>(value, y);
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDoubleSetY(SIMDVectorDouble value, f64 y)noexcept{
    return SIMDVectorDoubleDetail::SetLane<1u>(value, y);
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL VectorSetZ(SIMDVectorSingle value, f32 z)noexcept{
#if defined(FOUNDATION_HAS_SCALAR)
    value.f[2] = z;
    return value;
#elif defined(FOUNDATION_HAS_NEON)
    return vsetq_lane_f32(z, value, 2);
#elif defined(FOUNDATION_HAS_SSE4)
    return _mm_insert_ps(value, _mm_set_ss(z), s_SIMDSse4InsertLaneZMask);
#else
    return SIMDVectorDetail::SetLane<2u>(value, z);
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDoubleSetZ(SIMDVectorDouble value, f64 z)noexcept{
    return SIMDVectorDoubleDetail::SetLane<2u>(value, z);
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL VectorSetW(SIMDVectorSingle value, f32 w)noexcept{
#if defined(FOUNDATION_HAS_SCALAR)
    value.f[3] = w;
    return value;
#elif defined(FOUNDATION_HAS_NEON)
    return vsetq_lane_f32(w, value, 3);
#elif defined(FOUNDATION_HAS_SSE4)
    return _mm_insert_ps(value, _mm_set_ss(w), s_SIMDSse4InsertLaneWMask);
#else
    return SIMDVectorDetail::SetLane<3u>(value, w);
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDoubleSetW(SIMDVectorDouble value, f64 w)noexcept{
    return SIMDVectorDoubleDetail::SetLane<3u>(value, w);
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL VectorSetByIndex(SIMDVectorSingle value, f32 component, usize index)noexcept{
    FOUNDATION_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorSetX(value, component);
    case 1u: return VectorSetY(value, component);
    case 2u: return VectorSetZ(value, component);
    default: return VectorSetW(value, component);
    }
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDoubleSetByIndex(SIMDVectorDouble value, f64 component, usize index)noexcept{
    FOUNDATION_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleSetX(value, component);
    case 1u: return VectorDoubleSetY(value, component);
    case 2u: return VectorDoubleSetZ(value, component);
    default: return VectorDoubleSetW(value, component);
    }
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL VectorSetIntX(SIMDVectorSingle value, u32 x)noexcept{
#if defined(FOUNDATION_HAS_SCALAR)
    value.u[0] = x;
    return value;
#elif defined(FOUNDATION_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(x, vreinterpretq_u32_f32(value), 0));
#else
    return _mm_move_ss(value, _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<i32>(x))));
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDoubleSetIntX(SIMDVectorDouble value, u64 x)noexcept{
    return SIMDVectorDoubleDetail::SetLane<0u>(value, BitCast<f64>(x));
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL VectorSetIntY(SIMDVectorSingle value, u32 y)noexcept{
#if defined(FOUNDATION_HAS_SCALAR)
    value.u[1] = y;
    return value;
#elif defined(FOUNDATION_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(y, vreinterpretq_u32_f32(value), 1));
#elif defined(FOUNDATION_HAS_SSE4)
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(y), 1));
#else
    return SIMDVectorDetail::SetLane<1u>(value, BitCast<f32>(y));
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDoubleSetIntY(SIMDVectorDouble value, u64 y)noexcept{
    return SIMDVectorDoubleDetail::SetLane<1u>(value, BitCast<f64>(y));
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL VectorSetIntZ(SIMDVectorSingle value, u32 z)noexcept{
#if defined(FOUNDATION_HAS_SCALAR)
    value.u[2] = z;
    return value;
#elif defined(FOUNDATION_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(z, vreinterpretq_u32_f32(value), 2));
#elif defined(FOUNDATION_HAS_SSE4)
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(z), 2));
#else
    return SIMDVectorDetail::SetLane<2u>(value, BitCast<f32>(z));
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDoubleSetIntZ(SIMDVectorDouble value, u64 z)noexcept{
    return SIMDVectorDoubleDetail::SetLane<2u>(value, BitCast<f64>(z));
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL VectorSetIntW(SIMDVectorSingle value, u32 w)noexcept{
#if defined(FOUNDATION_HAS_SCALAR)
    value.u[3] = w;
    return value;
#elif defined(FOUNDATION_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(w, vreinterpretq_u32_f32(value), 3));
#elif defined(FOUNDATION_HAS_SSE4)
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(w), 3));
#else
    return SIMDVectorDetail::SetLane<3u>(value, BitCast<f32>(w));
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDoubleSetIntW(SIMDVectorDouble value, u64 w)noexcept{
    return SIMDVectorDoubleDetail::SetLane<3u>(value, BitCast<f64>(w));
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL VectorSetIntByIndex(SIMDVectorSingle value, u32 component, usize index)noexcept{
    FOUNDATION_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorSetIntX(value, component);
    case 1u: return VectorSetIntY(value, component);
    case 2u: return VectorSetIntZ(value, component);
    default: return VectorSetIntW(value, component);
    }
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDoubleSetIntByIndex(SIMDVectorDouble value, u64 component, usize index)noexcept{
    FOUNDATION_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleSetIntX(value, component);
    case 1u: return VectorDoubleSetIntY(value, component);
    case 2u: return VectorDoubleSetIntZ(value, component);
    default: return VectorDoubleSetIntW(value, component);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

