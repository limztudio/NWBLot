// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



[[nodiscard]] inline SIMDVectorSingle VectorQuantizeUNorm(const SIMDVectorSingle value, const SIMDVectorSingle maxValue)noexcept{
    const SIMDVectorSingle scaled = VectorMultiply(VectorSaturate(value), maxValue);
    return VectorMin(VectorFloor(VectorAdd(scaled, VectorReplicate(0.5f))), maxValue);
}

[[nodiscard]] inline SIMDVectorSingle VectorQuantizeSNorm(const SIMDVectorSingle value, const SIMDVectorSingle maxValue)noexcept{
    const SIMDVectorSingle clamped = VectorClamp(value, VectorReplicate(-1.0f), VectorReplicate(1.0f));
    const SIMDVectorSingle scaled = VectorMultiply(clamped, maxValue);
    const SIMDVectorSingle magnitude = VectorFloor(VectorAdd(VectorAbs(scaled), VectorReplicate(0.5f)));
    return VectorSelect(VectorNegate(magnitude), magnitude, VectorGreaterOrEqual(scaled, VectorZero()));
}

[[nodiscard]] inline SIMDVectorSingle VectorLinearToSRGB(const SIMDVectorSingle linear)noexcept{
    const SIMDVectorSingle clamped = VectorSaturate(linear);
    const SIMDVectorSingle threshold = VectorReplicate(0.0031308f);
    const SIMDVectorSingle linearPart = VectorMultiply(clamped, VectorReplicate(12.92f));
    const SIMDVectorSingle power = VectorPow(clamped, VectorReplicate(1.0f / 2.4f));
    const SIMDVectorSingle nonlinearPart = VectorSubtract(VectorMultiply(VectorReplicate(1.055f), power), VectorReplicate(0.055f));
    return VectorSelect(nonlinearPart, linearPart, VectorLessOrEqual(clamped, threshold));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

