// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_FrameDirectionEpsilon = 0.00000001f;
static constexpr f32 s_FrameHandednessEpsilon = 0.000001f;

[[nodiscard]] inline bool FrameFiniteVector(const SIMDVector value, const u32 activeMask){
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    return (VectorMoveMask(invalid) & activeMask) == 0u;
}

[[nodiscard]] inline bool FrameValidDirection(const SIMDVector value){
    return FrameFiniteVector(value, 0x7u)
        && VectorGetX(Vector3LengthSq(value)) > s_FrameDirectionEpsilon
    ;
}

[[nodiscard]] inline SIMDVector FrameNormalizeDirection(const SIMDVector value, const SIMDVector fallback){
    if(!FrameFiniteVector(value, 0x7u))
        return fallback;

    const f32 lengthSquared = VectorGetX(Vector3LengthSq(value));
    if(!IsFinite(lengthSquared) || lengthSquared <= s_FrameDirectionEpsilon)
        return fallback;

    return VectorMultiply(value, VectorReciprocalSqrt(VectorReplicate(lengthSquared)));
}

[[nodiscard]] inline SIMDVector FrameProjectOntoPlane(const SIMDVector value, const SIMDVector normal){
    return VectorMultiplyAdd(
        normal,
        VectorReplicate(-VectorGetX(Vector3Dot(value, normal))),
        value
    );
}

[[nodiscard]] inline SIMDVector FrameFallbackTangent(const SIMDVector normal){
    const SIMDVector axis = Abs(VectorGetZ(normal)) < 0.999f
        ? VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
        : VectorSet(0.0f, 1.0f, 0.0f, 0.0f)
    ;
    return FrameNormalizeDirection(Vector3Cross(axis, normal), VectorSet(1.0f, 0.0f, 0.0f, 0.0f));
}

[[nodiscard]] inline f32 FrameTangentHandedness(const f32 handedness, const f32 fallbackHandedness){
    if(Abs(handedness) > s_FrameHandednessEpsilon)
        return handedness < 0.0f ? -1.0f : 1.0f;
    return fallbackHandedness < 0.0f ? -1.0f : 1.0f;
}

[[nodiscard]] inline SIMDVector FrameResolveTangent(const SIMDVector normal, const SIMDVector tangent, const SIMDVector fallbackTangent){
    const SIMDVector safeFallbackTangent = FrameFallbackTangent(normal);

    SIMDVector projectedTangent = FrameFiniteVector(tangent, 0x7u)
        ? FrameProjectOntoPlane(tangent, normal)
        : safeFallbackTangent
    ;
    if(!FrameValidDirection(projectedTangent)){
        projectedTangent = FrameFiniteVector(fallbackTangent, 0x7u)
            ? FrameProjectOntoPlane(fallbackTangent, normal)
            : safeFallbackTangent
        ;
    }
    if(!FrameValidDirection(projectedTangent))
        return safeFallbackTangent;

    return FrameNormalizeDirection(projectedTangent, safeFallbackTangent);
}

[[nodiscard]] inline SIMDVector FrameResolveBitangent(const SIMDVector normal, const SIMDVector tangent, const SIMDVector fallbackBitangent){
    const SIMDVector safeFallbackBitangent = FrameNormalizeDirection(
        FrameFiniteVector(fallbackBitangent, 0x7u)
            ? FrameProjectOntoPlane(fallbackBitangent, normal)
            : Vector3Cross(normal, FrameFallbackTangent(normal)),
        VectorSet(0.0f, 1.0f, 0.0f, 0.0f)
    );

    SIMDVector bitangent = Vector3Cross(normal, tangent);
    if(!FrameValidDirection(bitangent))
        bitangent = safeFallbackBitangent;
    return FrameNormalizeDirection(bitangent, safeFallbackBitangent);
}

inline void FrameOrthonormalize(SIMDVector& normal, SIMDVector& tangent, const SIMDVector fallbackNormal, const SIMDVector fallbackTangent){
    normal = FrameNormalizeDirection(normal, FrameNormalizeDirection(fallbackNormal, VectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
    tangent = VectorSetW(
        FrameResolveTangent(normal, tangent, fallbackTangent),
        FrameTangentHandedness(VectorGetW(tangent), VectorGetW(fallbackTangent))
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

