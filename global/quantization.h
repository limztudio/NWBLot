// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "simd/vector_rounding.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline f32 QuantizeUNormScalar(const f32 value, const f32 maxValue)noexcept{
    const SIMDVectorSingle valueVec = VectorReplicate(value);
    const SIMDVectorSingle maxVec = VectorReplicate(maxValue);
    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle one = VectorSplatOne();
    const SIMDVectorSingle clampedLow = VectorSelect(valueVec, zero, VectorLess(valueVec, zero));
    const SIMDVectorSingle saturated = VectorSelect(clampedLow, one, VectorGreater(clampedLow, one));
    const SIMDVectorSingle scaled = VectorMultiply(saturated, maxVec);
    const SIMDVectorSingle rounded = VectorFloor(VectorAdd(scaled, VectorReplicate(s_RoundHalfBias)));
    return VectorGetX(VectorSelect(maxVec, rounded, VectorLess(rounded, maxVec)));
}

[[nodiscard]] inline f32 QuantizeSNormScalar(const f32 value, const f32 maxValue)noexcept{
    const SIMDVectorSingle valueVec = VectorReplicate(value);
    const SIMDVectorSingle maxVec = VectorReplicate(maxValue);
    const SIMDVectorSingle negOne = VectorReplicate(-1.0f);
    const SIMDVectorSingle one = VectorSplatOne();
    const SIMDVectorSingle clampedHigh = VectorSelect(valueVec, one, VectorGreater(valueVec, one));
    const SIMDVectorSingle clamped = VectorSelect(negOne, clampedHigh, VectorGreater(valueVec, negOne));
    const SIMDVectorSingle scaled = VectorMultiply(clamped, maxVec);
    const SIMDVectorSingle magnitude = VectorFloor(VectorAdd(VectorAbs(scaled), VectorReplicate(s_RoundHalfBias)));
    return VectorGetX(VectorSelect(VectorNegate(magnitude), magnitude, VectorGreaterOrEqual(scaled, VectorZero())));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

