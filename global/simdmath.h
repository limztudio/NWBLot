// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "simplemath.h"
#include "math/type.h"
#include "math/constant.h"
#include "math/convert.h"
#include "math/vector.h"
#include "math/quaternion.h"
#include "math/matrix.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float2Data& src)noexcept{
    return VectorSet(src.x, src.y, 0.0f, 0.0f);
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL LoadFloat(const AlignedFloat2Data& src)noexcept{
    return VectorSet(src.x, src.y, 0.0f, 0.0f);
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float3Data& src)noexcept{
    return VectorSet(src.x, src.y, src.z, 0.0f);
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL LoadFloat(const AlignedFloat3Data& src)noexcept{
    return VectorSet(src.x, src.y, src.z, 0.0f);
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float4Data& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(src.x, src.y, src.z, src.w);
#elif defined(NWB_HAS_NEON)
    return vld1q_f32(&src.x);
#else
    return _mm_loadu_ps(&src.x);
#endif
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL LoadFloat(const AlignedFloat4Data& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(src.x, src.y, src.z, src.w);
#elif defined(NWB_HAS_NEON)
    return vld1q_f32(&src.x);
#else
    return _mm_load_ps(&src.x);
#endif
}

NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float2Data* dst)noexcept{
    NWB_ASSERT(dst != nullptr);
    dst->x = VectorGetX(src);
    dst->y = VectorGetY(src);
}

NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, AlignedFloat2Data* dst)noexcept{
    NWB_ASSERT(dst != nullptr);
    dst->x = VectorGetX(src);
    dst->y = VectorGetY(src);
}

NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float3Data* dst)noexcept{
    NWB_ASSERT(dst != nullptr);
    dst->x = VectorGetX(src);
    dst->y = VectorGetY(src);
    dst->z = VectorGetZ(src);
}

NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, AlignedFloat3Data* dst)noexcept{
    NWB_ASSERT(dst != nullptr);
    dst->x = VectorGetX(src);
    dst->y = VectorGetY(src);
    dst->z = VectorGetZ(src);
}

NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float4Data* dst)noexcept{
    NWB_ASSERT(dst != nullptr);
#if defined(NWB_HAS_SCALAR)
    dst->x = VectorGetX(src);
    dst->y = VectorGetY(src);
    dst->z = VectorGetZ(src);
    dst->w = VectorGetW(src);
#elif defined(NWB_HAS_NEON)
    vst1q_f32(&dst->x, src);
#else
    _mm_storeu_ps(&dst->x, src);
#endif
}

NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, AlignedFloat4Data* dst)noexcept{
    NWB_ASSERT(dst != nullptr);
#if defined(NWB_HAS_SCALAR)
    dst->x = VectorGetX(src);
    dst->y = VectorGetY(src);
    dst->z = VectorGetZ(src);
    dst->w = VectorGetW(src);
#elif defined(NWB_HAS_NEON)
    vst1q_f32(&dst->x, src);
#else
    _mm_store_ps(&dst->x, src);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

