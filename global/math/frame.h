
#pragma once


#include "../simdmath.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_FrameDirectionEpsilon = 0.00000001f;
static constexpr f32 s_FrameHandednessEpsilon = 0.000001f;

[[nodiscard]] NWB_INLINE bool FrameValidDirection(const SIMDVector value){
    return VectorIsFinite(value, 0x7u) && Vector3Greater(Vector3LengthSq(value), VectorReplicate(s_FrameDirectionEpsilon));
}

[[nodiscard]] NWB_INLINE SIMDVector FrameNormalizeDirection(const SIMDVector value, const SIMDVector fallback){
    return Vector3NormalizeOr(value, fallback, s_FrameDirectionEpsilon);
}

[[nodiscard]] NWB_INLINE SIMDVector FrameProjectOntoPlane(const SIMDVector value, const SIMDVector normal){
    return VectorMultiplyAdd(
        normal,
        VectorNegate(Vector3Dot(value, normal)),
        value
    );
}

[[nodiscard]] NWB_INLINE SIMDVector FrameFallbackTangent(const SIMDVector normal){
    const SIMDVector zAxis = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    const SIMDVector yAxis = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVector axis = VectorSelect(
        yAxis,
        zAxis,
        VectorLess(VectorAbs(VectorSplatZ(normal)), VectorReplicate(0.999f))
    );
    return FrameNormalizeDirection(Vector3Cross(axis, normal), VectorSet(1.0f, 0.0f, 0.0f, 0.0f));
}

[[nodiscard]] NWB_INLINE f32 FrameTangentHandedness(const f32 handedness, const f32 fallbackHandedness){
    if(Abs(handedness) > s_FrameHandednessEpsilon)
        return handedness < 0.0f ? -1.0f : 1.0f;
    return fallbackHandedness < 0.0f ? -1.0f : 1.0f;
}

[[nodiscard]] NWB_INLINE SIMDVector FrameResolveTangent(const SIMDVector normal, const SIMDVector tangent, const SIMDVector fallbackTangent){
    const SIMDVector safeFallbackTangent = FrameFallbackTangent(normal);

    SIMDVector projectedTangent = VectorIsFinite(tangent, 0x7u)
        ? FrameProjectOntoPlane(tangent, normal)
        : safeFallbackTangent
    ;
    if(!FrameValidDirection(projectedTangent)){
        projectedTangent = VectorIsFinite(fallbackTangent, 0x7u)
            ? FrameProjectOntoPlane(fallbackTangent, normal)
            : safeFallbackTangent
        ;
    }
    if(!FrameValidDirection(projectedTangent))
        return safeFallbackTangent;

    return FrameNormalizeDirection(projectedTangent, safeFallbackTangent);
}

[[nodiscard]] NWB_INLINE SIMDVector FrameResolveBitangent(const SIMDVector normal, const SIMDVector tangent, const SIMDVector fallbackBitangent){
    const SIMDVector safeFallbackBitangent = FrameNormalizeDirection(
        VectorIsFinite(fallbackBitangent, 0x7u)
            ? FrameProjectOntoPlane(fallbackBitangent, normal)
            : Vector3Cross(normal, FrameFallbackTangent(normal)),
        VectorSet(0.0f, 1.0f, 0.0f, 0.0f)
    );

    SIMDVector bitangent = Vector3Cross(normal, tangent);
    if(!FrameValidDirection(bitangent))
        bitangent = safeFallbackBitangent;
    return FrameNormalizeDirection(bitangent, safeFallbackBitangent);
}

NWB_INLINE void FrameOrthonormalize(SIMDVector& normal, SIMDVector& tangent, const SIMDVector fallbackNormal, const SIMDVector fallbackTangent){
    normal = FrameNormalizeDirection(normal, FrameNormalizeDirection(fallbackNormal, VectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
    tangent = VectorSetW(
        FrameResolveTangent(normal, tangent, fallbackTangent),
        FrameTangentHandedness(VectorGetW(tangent), VectorGetW(fallbackTangent))
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

