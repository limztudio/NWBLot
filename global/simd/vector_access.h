// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_construct.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE f32 NWB_SIMD_CALL VectorGetX(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetLane<0u>(value); }

NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetX(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<0u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetXPtr(f32& out, SIMDVectorSingle value)noexcept{ out = VectorGetX(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetXPtr(f64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetX(value); }

NWB_INLINE f32 NWB_SIMD_CALL VectorGetY(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetLane<1u>(value); }

NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetY(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<1u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetYPtr(f32& out, SIMDVectorSingle value)noexcept{ out = VectorGetY(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetYPtr(f64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetY(value); }

NWB_INLINE f32 NWB_SIMD_CALL VectorGetZ(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetLane<2u>(value); }

NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetZ(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<2u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetZPtr(f32& out, SIMDVectorSingle value)noexcept{ out = VectorGetZ(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetZPtr(f64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetZ(value); }

NWB_INLINE f32 NWB_SIMD_CALL VectorGetW(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetLane<3u>(value); }

NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetW(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<3u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetWPtr(f32& out, SIMDVectorSingle value)noexcept{ out = VectorGetW(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetWPtr(f64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetW(value); }

NWB_INLINE f32 NWB_SIMD_CALL VectorGetByIndex(SIMDVectorSingle value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorGetX(value);
    case 1u: return VectorGetY(value);
    case 2u: return VectorGetZ(value);
    default: return VectorGetW(value);
    }
}

NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetByIndex(SIMDVectorDouble value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleGetX(value);
    case 1u: return VectorDoubleGetY(value);
    case 2u: return VectorDoubleGetZ(value);
    default: return VectorDoubleGetW(value);
    }
}

NWB_INLINE void NWB_SIMD_CALL VectorGetByIndexPtr(f32& out, SIMDVectorSingle value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    out = VectorGetByIndex(value, index);
}

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetByIndexPtr(f64& out, SIMDVectorDouble value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    out = VectorDoubleGetByIndex(value, index);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorGetIntX(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetIntLane<0u>(value); }

NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntX(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<0u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetIntXPtr(u32& out, SIMDVectorSingle value)noexcept{ out = VectorGetIntX(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntXPtr(u64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetIntX(value); }

NWB_INLINE u32 NWB_SIMD_CALL VectorGetIntY(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetIntLane<1u>(value); }

NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntY(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<1u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetIntYPtr(u32& out, SIMDVectorSingle value)noexcept{ out = VectorGetIntY(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntYPtr(u64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetIntY(value); }

NWB_INLINE u32 NWB_SIMD_CALL VectorGetIntZ(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetIntLane<2u>(value); }

NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntZ(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<2u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetIntZPtr(u32& out, SIMDVectorSingle value)noexcept{ out = VectorGetIntZ(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntZPtr(u64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetIntZ(value); }

NWB_INLINE u32 NWB_SIMD_CALL VectorGetIntW(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetIntLane<3u>(value); }

NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntW(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<3u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetIntWPtr(u32& out, SIMDVectorSingle value)noexcept{ out = VectorGetIntW(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntWPtr(u64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetIntW(value); }

NWB_INLINE u32 NWB_SIMD_CALL VectorGetIntByIndex(SIMDVectorSingle value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorGetIntX(value);
    case 1u: return VectorGetIntY(value);
    case 2u: return VectorGetIntZ(value);
    default: return VectorGetIntW(value);
    }
}

NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntByIndex(SIMDVectorDouble value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleGetIntX(value);
    case 1u: return VectorDoubleGetIntY(value);
    case 2u: return VectorDoubleGetIntZ(value);
    default: return VectorDoubleGetIntW(value);
    }
}

NWB_INLINE void NWB_SIMD_CALL VectorGetIntByIndexPtr(u32& out, SIMDVectorSingle value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    out = VectorGetIntByIndex(value, index);
}

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntByIndexPtr(u64& out, SIMDVectorDouble value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    out = VectorDoubleGetIntByIndex(value, index);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetX(SIMDVectorSingle value, f32 x)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.f[0] = x;
    return value;
#elif defined(NWB_HAS_NEON)
    return vsetq_lane_f32(x, value, 0);
#else
    return _mm_move_ss(value, _mm_set_ss(x));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetX(SIMDVectorDouble value, f64 x)noexcept{
    return SIMDVectorDoubleDetail::SetLane<0u>(value, x);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetXPtr(SIMDVectorSingle value, const f32 x)noexcept{
    return VectorSetX(value, x);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetXPtr(SIMDVectorDouble value, const f64 x)noexcept{
    return VectorDoubleSetX(value, x);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetY(SIMDVectorSingle value, f32 y)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.f[1] = y;
    return value;
#elif defined(NWB_HAS_NEON)
    return vsetq_lane_f32(y, value, 1);
#elif defined(NWB_HAS_SSE4)
    return _mm_insert_ps(value, _mm_set_ss(y), 0x10);
#else
    return SIMDVectorDetail::SetLane<1u>(value, y);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetY(SIMDVectorDouble value, f64 y)noexcept{
    return SIMDVectorDoubleDetail::SetLane<1u>(value, y);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetYPtr(SIMDVectorSingle value, const f32 y)noexcept{
    return VectorSetY(value, y);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetYPtr(SIMDVectorDouble value, const f64 y)noexcept{
    return VectorDoubleSetY(value, y);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetZ(SIMDVectorSingle value, f32 z)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.f[2] = z;
    return value;
#elif defined(NWB_HAS_NEON)
    return vsetq_lane_f32(z, value, 2);
#elif defined(NWB_HAS_SSE4)
    return _mm_insert_ps(value, _mm_set_ss(z), 0x20);
#else
    return SIMDVectorDetail::SetLane<2u>(value, z);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetZ(SIMDVectorDouble value, f64 z)noexcept{
    return SIMDVectorDoubleDetail::SetLane<2u>(value, z);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetZPtr(SIMDVectorSingle value, const f32 z)noexcept{
    return VectorSetZ(value, z);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetZPtr(SIMDVectorDouble value, const f64 z)noexcept{
    return VectorDoubleSetZ(value, z);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetW(SIMDVectorSingle value, f32 w)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.f[3] = w;
    return value;
#elif defined(NWB_HAS_NEON)
    return vsetq_lane_f32(w, value, 3);
#elif defined(NWB_HAS_SSE4)
    return _mm_insert_ps(value, _mm_set_ss(w), 0x30);
#else
    return SIMDVectorDetail::SetLane<3u>(value, w);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetW(SIMDVectorDouble value, f64 w)noexcept{
    return SIMDVectorDoubleDetail::SetLane<3u>(value, w);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetWPtr(SIMDVectorSingle value, const f32 w)noexcept{
    return VectorSetW(value, w);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetWPtr(SIMDVectorDouble value, const f64 w)noexcept{
    return VectorDoubleSetW(value, w);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetByIndex(SIMDVectorSingle value, f32 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorSetX(value, component);
    case 1u: return VectorSetY(value, component);
    case 2u: return VectorSetZ(value, component);
    default: return VectorSetW(value, component);
    }
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetByIndex(SIMDVectorDouble value, f64 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleSetX(value, component);
    case 1u: return VectorDoubleSetY(value, component);
    case 2u: return VectorDoubleSetZ(value, component);
    default: return VectorDoubleSetW(value, component);
    }
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetByIndexPtr(SIMDVectorSingle value, const f32 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    return VectorSetByIndex(value, component, index);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetByIndexPtr(SIMDVectorDouble value, const f64 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    return VectorDoubleSetByIndex(value, component, index);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntX(SIMDVectorSingle value, u32 x)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.u[0] = x;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(x, vreinterpretq_u32_f32(value), 0));
#else
    return _mm_move_ss(value, _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<i32>(x))));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntX(SIMDVectorDouble value, u64 x)noexcept{
    return SIMDVectorDoubleDetail::SetLane<0u>(value, BitCast<f64>(x));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntXPtr(SIMDVectorSingle value, const u32 x)noexcept{
    return VectorSetIntX(value, x);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntXPtr(SIMDVectorDouble value, const u64 x)noexcept{
    return VectorDoubleSetIntX(value, x);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntY(SIMDVectorSingle value, u32 y)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.u[1] = y;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(y, vreinterpretq_u32_f32(value), 1));
#elif defined(NWB_HAS_SSE4)
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(y), 1));
#else
    return SIMDVectorDetail::SetLane<1u>(value, BitCast<f32>(y));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntY(SIMDVectorDouble value, u64 y)noexcept{
    return SIMDVectorDoubleDetail::SetLane<1u>(value, BitCast<f64>(y));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntYPtr(SIMDVectorSingle value, const u32 y)noexcept{
    return VectorSetIntY(value, y);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntYPtr(SIMDVectorDouble value, const u64 y)noexcept{
    return VectorDoubleSetIntY(value, y);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntZ(SIMDVectorSingle value, u32 z)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.u[2] = z;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(z, vreinterpretq_u32_f32(value), 2));
#elif defined(NWB_HAS_SSE4)
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(z), 2));
#else
    return SIMDVectorDetail::SetLane<2u>(value, BitCast<f32>(z));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntZ(SIMDVectorDouble value, u64 z)noexcept{
    return SIMDVectorDoubleDetail::SetLane<2u>(value, BitCast<f64>(z));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntZPtr(SIMDVectorSingle value, const u32 z)noexcept{
    return VectorSetIntZ(value, z);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntZPtr(SIMDVectorDouble value, const u64 z)noexcept{
    return VectorDoubleSetIntZ(value, z);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntW(SIMDVectorSingle value, u32 w)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.u[3] = w;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(w, vreinterpretq_u32_f32(value), 3));
#elif defined(NWB_HAS_SSE4)
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(w), 3));
#else
    return SIMDVectorDetail::SetLane<3u>(value, BitCast<f32>(w));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntW(SIMDVectorDouble value, u64 w)noexcept{
    return SIMDVectorDoubleDetail::SetLane<3u>(value, BitCast<f64>(w));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntWPtr(SIMDVectorSingle value, const u32 w)noexcept{
    return VectorSetIntW(value, w);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntWPtr(SIMDVectorDouble value, const u64 w)noexcept{
    return VectorDoubleSetIntW(value, w);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntByIndex(SIMDVectorSingle value, u32 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorSetIntX(value, component);
    case 1u: return VectorSetIntY(value, component);
    case 2u: return VectorSetIntZ(value, component);
    default: return VectorSetIntW(value, component);
    }
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntByIndex(SIMDVectorDouble value, u64 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleSetIntX(value, component);
    case 1u: return VectorDoubleSetIntY(value, component);
    case 2u: return VectorDoubleSetIntZ(value, component);
    default: return VectorDoubleSetIntW(value, component);
    }
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntByIndexPtr(SIMDVectorSingle value, const u32 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    return VectorSetIntByIndex(value, component, index);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntByIndexPtr(SIMDVectorDouble value, const u64 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    return VectorDoubleSetIntByIndex(value, component, index);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

