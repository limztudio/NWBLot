// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_FrameDirectionEpsilon = 0.00000001f;
static constexpr f32 s_FrameHandednessEpsilon = 0.000001f;
static constexpr f32 s_FrameFallbackTangentZAxisAlignmentThreshold = 0.999f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE b8 FrameValidDirection(const SIMDVectorSingle value)noexcept{
    return Vector3IsFinite(value) && Vector3Greater(Vector3LengthSq(value), VectorReplicate(s_FrameDirectionEpsilon));
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle FrameNormalizeDirection(const SIMDVectorSingle value, const SIMDVectorSingle fallback)noexcept{
    return Vector3NormalizeOr(value, fallback, s_FrameDirectionEpsilon);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle FrameProjectOntoPlane(const SIMDVectorSingle value, const SIMDVectorSingle normal)noexcept{
    return VectorMultiplyAdd(normal, VectorNegate(Vector3Dot(value, normal)), value);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle FrameFallbackTangent(const SIMDVectorSingle normal)noexcept{
    const SIMDVectorSingle zAxis = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    const SIMDVectorSingle yAxis = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVectorSingle axis = VectorSelect(yAxis, zAxis, VectorLess(VectorAbs(VectorSplatZ(normal)), VectorReplicate(s_FrameFallbackTangentZAxisAlignmentThreshold)));
    return FrameNormalizeDirection(Vector3Cross(axis, normal), VectorSet(1.0f, 0.0f, 0.0f, 0.0f));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle FrameTangentHandedness(const SIMDVectorSingle handedness, const SIMDVectorSingle fallbackHandedness)noexcept{
    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle sign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, VectorLess(handedness, zero));
    const SIMDVectorSingle fallbackSign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, VectorLess(fallbackHandedness, zero));
    return VectorSelect(fallbackSign, sign, VectorGreater(VectorAbs(handedness), VectorReplicate(s_FrameHandednessEpsilon)));
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle FrameResolveTangent(const SIMDVectorSingle normal, const SIMDVectorSingle tangent, const SIMDVectorSingle fallbackTangent)noexcept{
    const SIMDVectorSingle safeFallbackTangent = FrameFallbackTangent(normal);

    SIMDVectorSingle projectedTangent = Vector3IsFinite(tangent) ? FrameProjectOntoPlane(tangent, normal) : safeFallbackTangent;
    if(!FrameValidDirection(projectedTangent)){
        projectedTangent = Vector3IsFinite(fallbackTangent) ? FrameProjectOntoPlane(fallbackTangent, normal) : safeFallbackTangent;
    }
    if(!FrameValidDirection(projectedTangent))
        return safeFallbackTangent;

    return FrameNormalizeDirection(projectedTangent, safeFallbackTangent);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

