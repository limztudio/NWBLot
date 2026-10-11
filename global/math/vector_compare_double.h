// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_construct_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return BitCast<SIMDVectorDouble>(v0 == v1);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorDoubleEqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return SIMDVectorDoubleDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleEqual(v0, v1)), VectorDoubleComponentMask::s_DoubleXYZW);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleEqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits == v1Bits);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorDoubleEqualIntR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return SIMDVectorDoubleDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleEqualInt(v0, v1)), VectorDoubleComponentMask::s_DoubleXYZW);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNearEqual(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble epsilon)noexcept{
    return BitCast<SIMDVectorDouble>(__builtin_elementwise_abs(v0 - v1) <= epsilon);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNotEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return BitCast<SIMDVectorDouble>(v0 != v1);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNotEqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits != v1Bits);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleGreater(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return BitCast<SIMDVectorDouble>(v0 > v1);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleGreaterOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return BitCast<SIMDVectorDouble>(v0 >= v1);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleLess(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return BitCast<SIMDVectorDouble>(v0 < v1);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleLessOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return BitCast<SIMDVectorDouble>(v0 <= v1);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorDoubleGreaterR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return SIMDVectorDoubleDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleGreater(v0, v1)), VectorDoubleComponentMask::s_DoubleXYZW);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorDoubleGreaterOrEqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return SIMDVectorDoubleDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(v0, v1)), VectorDoubleComponentMask::s_DoubleXYZW);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleInBounds(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{
    return VectorDoubleLessOrEqual(VectorDoubleAndInt(value, s_DoubleSIMDAbsMask), bounds);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorDoubleInBoundsR(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{
    return SIMDVectorDoubleDetail::BoundsMaskR(VectorDoubleMoveMask(VectorDoubleInBounds(value, bounds)), VectorDoubleComponentMask::s_DoubleXYZW);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleEqualR(u32& outCR, SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    outCR = VectorDoubleEqualR(v0, v1);
    return VectorDoubleEqual(v0, v1);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleEqualIntR(u32& outCR, SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    outCR = VectorDoubleEqualIntR(v0, v1);
    return VectorDoubleEqualInt(v0, v1);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleGreaterR(u32& outCR, SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    outCR = VectorDoubleGreaterR(v0, v1);
    return VectorDoubleGreater(v0, v1);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleGreaterOrEqualR(u32& outCR, SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    outCR = VectorDoubleGreaterOrEqualR(v0, v1);
    return VectorDoubleGreaterOrEqual(v0, v1);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleInBoundsR(u32& outCR, SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{
    outCR = VectorDoubleInBoundsR(value, bounds);
    return VectorDoubleInBounds(value, bounds);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleIsNaN(SIMDVectorDouble value)noexcept{
    return VectorDoubleNotEqual(value, value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleIsInfinite(SIMDVectorDouble value)noexcept{
    return VectorDoubleEqualInt(VectorDoubleAndInt(value, s_DoubleSIMDAbsMask), s_DoubleSIMDInfinity);
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL VectorDoubleIsFinite(SIMDVectorDouble value, u32 activeMask)noexcept{
    const SIMDVectorDouble invalid = VectorDoubleOrInt(VectorDoubleIsNaN(value), VectorDoubleIsInfinite(value));
    return (VectorDoubleMoveMask(invalid) & activeMask) == 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

