// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "simplemath.h"
#include "simd/vector_arithmetic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline f32 QuantizeUNormScalar(const f32 value, const f32 maxValue)noexcept{
    const SIMDVectorSingle scaledLanes = VectorMultiply(VectorReplicate(Saturate(value)), VectorReplicate(maxValue));
    const f32 scaled = VectorGetX(scaledLanes);
    return Min(Floor(VectorGetX(VectorAdd(VectorReplicate(scaled), VectorReplicate(0.5f)))), maxValue);
}

[[nodiscard]] inline f32 QuantizeSNormScalar(const f32 value, const f32 maxValue)noexcept{
    const f32 clamped = Clamp(value, -1.0f, 1.0f);
    const f32 scaled = VectorGetX(VectorMultiply(VectorReplicate(clamped), VectorReplicate(maxValue)));
    const f32 magnitude = Floor(VectorGetX(VectorAdd(VectorReplicate(Abs(scaled)), VectorReplicate(0.5f))));
    return scaled >= 0.0f ? magnitude : VectorGetX(VectorNegate(VectorReplicate(magnitude)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

