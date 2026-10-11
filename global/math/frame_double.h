// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../simdmath.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f64 s_DoubleFrameDirectionEpsilon = 1.0e-24;
static constexpr f64 s_DoubleFrameHandednessEpsilon = 1.0e-12;
static constexpr f64 s_DoubleFrameFallbackTangentZAxisAlignmentThreshold = 0.999;

[[nodiscard]] NWB_INLINE bool FrameDoubleValidDirection(const SIMDVectorDouble value)noexcept{
    return VectorDouble3IsFinite(value) && VectorDouble3Greater(VectorDouble3LengthSq(value), VectorDoubleReplicate(s_DoubleFrameDirectionEpsilon));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble FrameDoubleNormalizeDirection(const SIMDVectorDouble value, const SIMDVectorDouble fallback)noexcept{
    return VectorDouble3NormalizeOr(value, fallback, s_DoubleFrameDirectionEpsilon);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble FrameDoubleProjectOntoPlane(const SIMDVectorDouble value, const SIMDVectorDouble normal)noexcept{
    return VectorDoubleMultiplyAdd(
        normal,
        VectorDoubleNegate(VectorDouble3Dot(value, normal)),
        value
    );
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble FrameDoubleFallbackTangent(const SIMDVectorDouble normal)noexcept{
    const SIMDVectorDouble zAxis = VectorDoubleSet(0.0, 0.0, 1.0, 0.0);
    const SIMDVectorDouble yAxis = VectorDoubleSet(0.0, 1.0, 0.0, 0.0);
    const SIMDVectorDouble axis = VectorDoubleSelect(
        yAxis,
        zAxis,
        VectorDoubleLess(VectorDoubleAbs(VectorDoubleSplatZ(normal)), VectorDoubleReplicate(s_DoubleFrameFallbackTangentZAxisAlignmentThreshold))
    );
    return FrameDoubleNormalizeDirection(VectorDouble3Cross(axis, normal), VectorDoubleSet(1.0, 0.0, 0.0, 0.0));
}

[[nodiscard]] NWB_INLINE f64 FrameDoubleTangentHandedness(const f64 handedness, const f64 fallbackHandedness)noexcept{
    if(Abs(handedness) > s_DoubleFrameHandednessEpsilon)
        return handedness < 0.0 ? -1.0 : 1.0;
    return fallbackHandedness < 0.0 ? -1.0 : 1.0;
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble FrameDoubleTangentHandedness(const SIMDVectorDouble handedness, const SIMDVectorDouble fallbackHandedness)noexcept{
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble sign = VectorDoubleSelect(s_DoubleSIMDOne, s_DoubleSIMDNegativeOne, VectorDoubleLess(handedness, zero));
    const SIMDVectorDouble fallbackSign = VectorDoubleSelect(s_DoubleSIMDOne, s_DoubleSIMDNegativeOne, VectorDoubleLess(fallbackHandedness, zero));
    return VectorDoubleSelect(fallbackSign, sign, VectorDoubleGreater(VectorDoubleAbs(handedness), VectorDoubleReplicate(s_DoubleFrameHandednessEpsilon)));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble FrameDoubleResolveTangent(const SIMDVectorDouble normal, const SIMDVectorDouble tangent, const SIMDVectorDouble fallbackTangent)noexcept{
    const SIMDVectorDouble safeFallbackTangent = FrameDoubleFallbackTangent(normal);

    SIMDVectorDouble projectedTangent = VectorDouble3IsFinite(tangent)
        ? FrameDoubleProjectOntoPlane(tangent, normal)
        : safeFallbackTangent
    ;
    if(!FrameDoubleValidDirection(projectedTangent)){
        projectedTangent = VectorDouble3IsFinite(fallbackTangent)
            ? FrameDoubleProjectOntoPlane(fallbackTangent, normal)
            : safeFallbackTangent
        ;
    }
    if(!FrameDoubleValidDirection(projectedTangent))
        return safeFallbackTangent;

    return FrameDoubleNormalizeDirection(projectedTangent, safeFallbackTangent);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble FrameDoubleResolveBitangent(const SIMDVectorDouble normal, const SIMDVectorDouble tangent, const SIMDVectorDouble fallbackBitangent)noexcept{
    const SIMDVectorDouble safeFallbackBitangent = FrameDoubleNormalizeDirection(
        VectorDouble3IsFinite(fallbackBitangent)
            ? FrameDoubleProjectOntoPlane(fallbackBitangent, normal)
            : VectorDouble3Cross(normal, FrameDoubleFallbackTangent(normal)),
        VectorDoubleSet(0.0, 1.0, 0.0, 0.0)
    );

    SIMDVectorDouble bitangent = VectorDouble3Cross(normal, tangent);
    if(!FrameDoubleValidDirection(bitangent))
        bitangent = safeFallbackBitangent;
    return FrameDoubleNormalizeDirection(bitangent, safeFallbackBitangent);
}

NWB_INLINE void FrameDoubleOrthonormalize(SIMDVectorDouble& normal, SIMDVectorDouble& tangent, const SIMDVectorDouble fallbackNormal, const SIMDVectorDouble fallbackTangent)noexcept{
    normal = FrameDoubleNormalizeDirection(normal, FrameDoubleNormalizeDirection(fallbackNormal, VectorDoubleSet(0.0, 0.0, 1.0, 0.0)));
    tangent = VectorDoubleSelect(
        FrameDoubleResolveTangent(normal, tangent, fallbackTangent),
        FrameDoubleTangentHandedness(tangent, fallbackTangent),
        s_DoubleSIMDMaskW
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

