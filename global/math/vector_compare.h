// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_construct.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL VectorEqual(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetX(v0) == VectorGetX(v1) ? 0xFFFFFFFFu : 0u, VectorGetY(v0) == VectorGetY(v1) ? 0xFFFFFFFFu : 0u, VectorGetZ(v0) == VectorGetZ(v1) ? 0xFFFFFFFFu : 0u, VectorGetW(v0) == VectorGetW(v1) ? 0xFFFFFFFFu : 0u);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vceqq_f32(v0, v1));
#else
    return _mm_cmpeq_ps(v0, v1);
#endif
}

NWB_INLINE u32 SIMDCALL VectorEqualR(SIMDVector v0, SIMDVector v1)noexcept{
    return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqual(v0, v1)), VectorComponentMask::s_XYZW);
}

NWB_INLINE SIMDVector SIMDCALL VectorEqualInt(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) == VectorGetIntX(v1) ? 0xFFFFFFFFu : 0u, VectorGetIntY(v0) == VectorGetIntY(v1) ? 0xFFFFFFFFu : 0u, VectorGetIntZ(v0) == VectorGetIntZ(v1) ? 0xFFFFFFFFu : 0u, VectorGetIntW(v0) == VectorGetIntW(v1) ? 0xFFFFFFFFu : 0u);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vceqq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_castps_si128(v0), _mm_castps_si128(v1)));
#endif
}

NWB_INLINE u32 SIMDCALL VectorEqualIntR(SIMDVector v0, SIMDVector v1)noexcept{
    return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqualInt(v0, v1)), VectorComponentMask::s_XYZW);
}

NWB_INLINE SIMDVector SIMDCALL VectorNearEqual(SIMDVector v0, SIMDVector v1, SIMDVector epsilon)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(
        Abs(VectorGetX(v0) - VectorGetX(v1)) <= VectorGetX(epsilon) ? 0xFFFFFFFFu : 0u,
        Abs(VectorGetY(v0) - VectorGetY(v1)) <= VectorGetY(epsilon) ? 0xFFFFFFFFu : 0u,
        Abs(VectorGetZ(v0) - VectorGetZ(v1)) <= VectorGetZ(epsilon) ? 0xFFFFFFFFu : 0u,
        Abs(VectorGetW(v0) - VectorGetW(v1)) <= VectorGetW(epsilon) ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcleq_f32(vabsq_f32(vsubq_f32(v0, v1)), epsilon));
#else
    const SIMDVector delta = _mm_andnot_ps(s_SIMDNegativeZero, _mm_sub_ps(v0, v1));
    return _mm_cmple_ps(delta, epsilon);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorNotEqual(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(
        v0.f[0] != v1.f[0] ? 0xFFFFFFFFu : 0u,
        v0.f[1] != v1.f[1] ? 0xFFFFFFFFu : 0u,
        v0.f[2] != v1.f[2] ? 0xFFFFFFFFu : 0u,
        v0.f[3] != v1.f[3] ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vmvnq_u32(vceqq_f32(v0, v1)));
#else
    return _mm_cmpneq_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorNotEqualInt(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(
        v0.u[0] != v1.u[0] ? 0xFFFFFFFFu : 0u,
        v0.u[1] != v1.u[1] ? 0xFFFFFFFFu : 0u,
        v0.u[2] != v1.u[2] ? 0xFFFFFFFFu : 0u,
        v0.u[3] != v1.u[3] ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vmvnq_u32(vceqq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1))));
#else
    const __m128i equal = _mm_cmpeq_epi32(_mm_castps_si128(v0), _mm_castps_si128(v1));
    return _mm_xor_ps(_mm_castsi128_ps(equal), s_SIMDNegOneMask);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorGreater(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcgtq_f32(v0, v1));
#elif defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetX(v0) > VectorGetX(v1) ? 0xFFFFFFFFu : 0u, VectorGetY(v0) > VectorGetY(v1) ? 0xFFFFFFFFu : 0u, VectorGetZ(v0) > VectorGetZ(v1) ? 0xFFFFFFFFu : 0u, VectorGetW(v0) > VectorGetW(v1) ? 0xFFFFFFFFu : 0u);
#else
    return _mm_cmpgt_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorGreaterOrEqual(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetX(v0) >= VectorGetX(v1) ? 0xFFFFFFFFu : 0u, VectorGetY(v0) >= VectorGetY(v1) ? 0xFFFFFFFFu : 0u, VectorGetZ(v0) >= VectorGetZ(v1) ? 0xFFFFFFFFu : 0u, VectorGetW(v0) >= VectorGetW(v1) ? 0xFFFFFFFFu : 0u);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcgeq_f32(v0, v1));
#else
    return _mm_cmpge_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorLess(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(
        v0.f[0] < v1.f[0] ? 0xFFFFFFFFu : 0u,
        v0.f[1] < v1.f[1] ? 0xFFFFFFFFu : 0u,
        v0.f[2] < v1.f[2] ? 0xFFFFFFFFu : 0u,
        v0.f[3] < v1.f[3] ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcltq_f32(v0, v1));
#else
    return _mm_cmplt_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorLessOrEqual(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(
        v0.f[0] <= v1.f[0] ? 0xFFFFFFFFu : 0u,
        v0.f[1] <= v1.f[1] ? 0xFFFFFFFFu : 0u,
        v0.f[2] <= v1.f[2] ? 0xFFFFFFFFu : 0u,
        v0.f[3] <= v1.f[3] ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcleq_f32(v0, v1));
#else
    return _mm_cmple_ps(v0, v1);
#endif
}

NWB_INLINE u32 SIMDCALL VectorGreaterR(SIMDVector v0, SIMDVector v1)noexcept{
    return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreater(v0, v1)), VectorComponentMask::s_XYZW);
}

NWB_INLINE u32 SIMDCALL VectorGreaterOrEqualR(SIMDVector v0, SIMDVector v1)noexcept{
    return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreaterOrEqual(v0, v1)), VectorComponentMask::s_XYZW);
}

NWB_INLINE SIMDVector SIMDCALL VectorInBounds(SIMDVector value, SIMDVector bounds)noexcept{
    return VectorLessOrEqual(VectorAndInt(value, s_SIMDAbsMask), bounds);
}

NWB_INLINE u32 SIMDCALL VectorInBoundsR(SIMDVector value, SIMDVector bounds)noexcept{
    return SIMDVectorDetail::BoundsMaskR(VectorMoveMask(VectorInBounds(value, bounds)), VectorComponentMask::s_XYZW);
}

NWB_INLINE SIMDVector SIMDCALL VectorEqualR(u32& outCR, SIMDVector v0, SIMDVector v1)noexcept{
    outCR = VectorEqualR(v0, v1);
    return VectorEqual(v0, v1);
}

NWB_INLINE SIMDVector SIMDCALL VectorEqualIntR(u32& outCR, SIMDVector v0, SIMDVector v1)noexcept{
    outCR = VectorEqualIntR(v0, v1);
    return VectorEqualInt(v0, v1);
}

NWB_INLINE SIMDVector SIMDCALL VectorGreaterR(u32& outCR, SIMDVector v0, SIMDVector v1)noexcept{
    outCR = VectorGreaterR(v0, v1);
    return VectorGreater(v0, v1);
}

NWB_INLINE SIMDVector SIMDCALL VectorGreaterOrEqualR(u32& outCR, SIMDVector v0, SIMDVector v1)noexcept{
    outCR = VectorGreaterOrEqualR(v0, v1);
    return VectorGreaterOrEqual(v0, v1);
}

NWB_INLINE SIMDVector SIMDCALL VectorInBoundsR(u32& outCR, SIMDVector value, SIMDVector bounds)noexcept{
    outCR = VectorInBoundsR(value, bounds);
    return VectorInBounds(value, bounds);
}

NWB_INLINE SIMDVector SIMDCALL VectorIsNaN(SIMDVector value)noexcept{
    return VectorNotEqual(value, value);
}

NWB_INLINE SIMDVector SIMDCALL VectorIsInfinite(SIMDVector value)noexcept{
    return VectorEqualInt(VectorAndInt(value, s_SIMDAbsMask), s_SIMDInfinity);
}

[[nodiscard]] NWB_INLINE bool SIMDCALL VectorIsFinite(SIMDVector value, u32 activeMask)noexcept{
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    return (VectorMoveMask(invalid) & activeMask) == 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

