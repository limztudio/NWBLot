// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "macro.h"
#include "constant.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL VectorZero()noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDVector{ 0.f, 0.f, 0.f, 0.f };
#elif defined (NWB_HAS_NEON)
    return vdupq_n_f32(0);
#elif defined(NWB_HAS_SSE4)
    return _mm_setzero_ps();
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSet(f32 x, f32 y, f32 z, f32 w)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDVector{ x, y, z, w };
#elif defined (NWB_HAS_NEON)
    float32x2_t V0 = vcreate_f32(static_cast<u64>(*reinterpret_cast<const u32*>(&x)) | (static_cast<u64>(*reinterpret_cast<const u32*>(&y)) << 32));
    float32x2_t V1 = vcreate_f32(static_cast<u64>(*reinterpret_cast<const u32*>(&z)) | (static_cast<u64>(*reinterpret_cast<const u32*>(&w)) << 32));
    return vcombine_f32(V0, V1);
#elif defined(NWB_HAS_SSE4)
    return _mm_set_ps(w, z, y, x);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

