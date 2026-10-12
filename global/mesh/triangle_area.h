// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../simd/vector.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Promote positions before subtracting so the area normal retains binary64 edge precision.
[[nodiscard]] NWB_INLINE SIMDVectorDouble BuildTriangleAreaNormal64(const SIMDVectorSingle a, const SIMDVectorSingle b, const SIMDVectorSingle c)noexcept{
    const SIMDVectorDouble origin = ConvertVectorSingleToDouble(VectorAndInt(a, s_SIMDMask3));
    const SIMDVectorDouble ab = VectorDoubleSubtract(ConvertVectorSingleToDouble(VectorAndInt(b, s_SIMDMask3)), origin);
    const SIMDVectorDouble ac = VectorDoubleSubtract(ConvertVectorSingleToDouble(VectorAndInt(c, s_SIMDMask3)), origin);
    const SIMDVectorDouble positive = VectorDoubleMultiply(VectorDoubleSwizzle<1, 2, 0, 3>(ab), VectorDoubleSwizzle<2, 0, 1, 3>(ac));
    const SIMDVectorDouble negative = VectorDoubleMultiply(VectorDoubleSwizzle<2, 0, 1, 3>(ab), VectorDoubleSwizzle<1, 2, 0, 3>(ac));
    return VectorDoubleAndInt(VectorDoubleSubtract(positive, negative), s_DoubleSIMDMask3);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble TriangleAreaNormalLengthSquared(SIMDVectorDouble areaNormal)noexcept{
#if defined(NWB_HAS_AVX2)
    // Retain the original AVX2 reduction order: (x squared + z squared) + y squared.
    areaNormal = VectorDoubleSwizzle<0, 2, 1, 3>(areaNormal);
#endif
#if defined(NWB_HAS_NEON64)
    // The previous ARM64 final multiply-add followed the compiler's contraction policy.
    const SIMDVectorDouble z = VectorDoubleSplatZ(areaNormal);
    return VectorDoubleMultiplyAddExpression(z, z, VectorDouble2LengthSq(areaNormal));
#else
    return VectorDouble3LengthSq(areaNormal);
#endif
}

[[nodiscard]] NWB_INLINE bool TriangleHasArea(const SIMDVectorSingle a, const SIMDVectorSingle b, const SIMDVectorSingle c, const f64 triangleAreaLengthSquaredEpsilon)noexcept{
    return VectorDoubleGetX(TriangleAreaNormalLengthSquared(BuildTriangleAreaNormal64(a, b, c))) > triangleAreaLengthSquaredEpsilon;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

