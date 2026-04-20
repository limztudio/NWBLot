// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"
#include "constant.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float3& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDVector{ src.x, src.y, src.z, 0.0f };
#elif defined (NWB_HAS_NEON)
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    float32x4_t v = vld1q_f32_ex(src.raw, 128);
#else
    float32x4_t v = vld1q_f32(src.raw);
#endif
    return vsetq_lane_f32(0, v, 3);
#elif defined(NWB_HAS_SSE4)
    __m128 v = _mm_load_ps(src.raw);
    return _mm_and_ps(v, s_SIMDMask3);
#endif
}
NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float4& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDVector{ src.x, src.y, src.z, src.w };
#elif defined (NWB_HAS_NEON)
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    return vld1q_f32_ex(src.raw, 128);
#else
    return vld1q_f32(src.raw);
#endif
#elif defined(NWB_HAS_SSE4)
    return _mm_load_ps(src.raw);
#endif
}
NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float34& src)noexcept{
}
NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float44& src)noexcept{
}

NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float2U& src)noexcept{
}
NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float3U& src)noexcept{
}
NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float4U& src)noexcept{
}
NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float34U& src)noexcept{
}
NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float44U& src)noexcept{
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float3* dst)noexcept{
}
NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float4* dst)noexcept{
}
NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float34* dst)noexcept{
}
NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float44* dst)noexcept{
}

NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float2U* dst)noexcept{
}
NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float3U* dst)noexcept{
}
NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float4U* dst)noexcept{
}
NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float34U* dst)noexcept{
}
NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float44U* dst)noexcept{
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

