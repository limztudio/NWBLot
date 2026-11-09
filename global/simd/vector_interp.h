// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_inverse_trig.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorSingle VectorWeightedSum(const SIMDVectorSingle first, const SIMDVectorSingle second, const SIMDVectorSingle firstWeight, const SIMDVectorSingle secondWeight)noexcept{
    return VectorAdd(VectorMultiply(first, firstWeight), VectorMultiply(second, secondWeight));
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL VectorLerp(SIMDVectorSingle v0, SIMDVectorSingle v1, f32 t)noexcept{
    return VectorMultiplyAdd(VectorSubtract(v1, v0), VectorReplicate(t), v0);
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDoubleLerp(SIMDVectorDouble v0, SIMDVectorDouble v1, f64 t)noexcept{
    return VectorDoubleMultiplyAdd(VectorDoubleSubtract(v1, v0), VectorDoubleReplicate(t), v0);
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL VectorLerpV(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle t)noexcept{
    return VectorMultiplyAdd(VectorSubtract(v1, v0), t, v0);
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDoubleLerpV(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble t)noexcept{
    return VectorDoubleMultiplyAdd(VectorDoubleSubtract(v1, v0), t, v0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

