// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../simdmath.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_FrameDirectionEpsilon = 0.00000001f;
static constexpr f64 s_DoubleFrameDirectionEpsilon = 1.0e-24;
static constexpr f32 s_FrameHandednessEpsilon = 0.000001f;
static constexpr f64 s_DoubleFrameHandednessEpsilon = 1.0e-12;
static constexpr f32 s_FrameFallbackTangentZAxisAlignmentThreshold = 0.999f;
static constexpr f64 s_DoubleFrameFallbackTangentZAxisAlignmentThreshold = 0.999;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool FrameValidDirection(const SIMDVectorSingle value)noexcept{
    return Vector3IsFinite(value) && Vector3Greater(Vector3LengthSq(value), VectorReplicate(s_FrameDirectionEpsilon));
}

[[nodiscard]] NWB_INLINE bool FrameDoubleValidDirection(const SIMDVectorDouble value)noexcept{
    return VectorDouble3IsFinite(value) && VectorDouble3Greater(VectorDouble3LengthSq(value), VectorDoubleReplicate(s_DoubleFrameDirectionEpsilon));
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle FrameNormalizeDirection(const SIMDVectorSingle value, const SIMDVectorSingle fallback)noexcept{
    return Vector3NormalizeOr(value, fallback, s_FrameDirectionEpsilon);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble FrameDoubleNormalizeDirection(const SIMDVectorDouble value, const SIMDVectorDouble fallback)noexcept{
    return VectorDouble3NormalizeOr(value, fallback, s_DoubleFrameDirectionEpsilon);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle FrameProjectOntoPlane(const SIMDVectorSingle value, const SIMDVectorSingle normal)noexcept{
    return VectorMultiplyAdd(normal, VectorNegate(Vector3Dot(value, normal)), value);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble FrameDoubleProjectOntoPlane(const SIMDVectorDouble value, const SIMDVectorDouble normal)noexcept{
    return VectorDoubleMultiplyAdd(normal, VectorDoubleNegate(VectorDouble3Dot(value, normal)), value);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle FrameFallbackTangent(const SIMDVectorSingle normal)noexcept{
    const SIMDVectorSingle zAxis = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    const SIMDVectorSingle yAxis = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVectorSingle axis = VectorSelect(yAxis, zAxis, VectorLess(VectorAbs(VectorSplatZ(normal)), VectorReplicate(s_FrameFallbackTangentZAxisAlignmentThreshold)));
    return FrameNormalizeDirection(Vector3Cross(axis, normal), VectorSet(1.0f, 0.0f, 0.0f, 0.0f));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble FrameDoubleFallbackTangent(const SIMDVectorDouble normal)noexcept{
    const SIMDVectorDouble zAxis = VectorDoubleSet(0.0, 0.0, 1.0, 0.0);
    const SIMDVectorDouble yAxis = VectorDoubleSet(0.0, 1.0, 0.0, 0.0);
    const SIMDVectorDouble axis = VectorDoubleSelect(yAxis, zAxis, VectorDoubleLess(VectorDoubleAbs(VectorDoubleSplatZ(normal)), VectorDoubleReplicate(s_DoubleFrameFallbackTangentZAxisAlignmentThreshold)));
    return FrameDoubleNormalizeDirection(VectorDouble3Cross(axis, normal), VectorDoubleSet(1.0, 0.0, 0.0, 0.0));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T> requires(IsSame_V<T, f32> || IsSame_V<T, f64>)
[[nodiscard]] NWB_INLINE T FrameTangentHandedness(const T handedness, const T fallbackHandedness)noexcept{
    constexpr T s_HandednessEpsilon = IsSame_V<T, f32> ? static_cast<T>(s_FrameHandednessEpsilon) : static_cast<T>(s_DoubleFrameHandednessEpsilon);
    if(Abs(handedness) > s_HandednessEpsilon)
        return handedness < T{ 0 } ? T{ -1 } : T{ 1 };
    return fallbackHandedness < T{ 0 } ? T{ -1 } : T{ 1 };
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle FrameTangentHandedness(const SIMDVectorSingle handedness, const SIMDVectorSingle fallbackHandedness)noexcept{
    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle sign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, VectorLess(handedness, zero));
    const SIMDVectorSingle fallbackSign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, VectorLess(fallbackHandedness, zero));
    return VectorSelect(fallbackSign, sign, VectorGreater(VectorAbs(handedness), VectorReplicate(s_FrameHandednessEpsilon)));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble FrameDoubleTangentHandedness(const SIMDVectorDouble handedness, const SIMDVectorDouble fallbackHandedness)noexcept{
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble sign = VectorDoubleSelect(s_DoubleSIMDOne, s_DoubleSIMDNegativeOne, VectorDoubleLess(handedness, zero));
    const SIMDVectorDouble fallbackSign = VectorDoubleSelect(s_DoubleSIMDOne, s_DoubleSIMDNegativeOne, VectorDoubleLess(fallbackHandedness, zero));
    return VectorDoubleSelect(fallbackSign, sign, VectorDoubleGreater(VectorDoubleAbs(handedness), VectorDoubleReplicate(s_DoubleFrameHandednessEpsilon)));
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

[[nodiscard]] NWB_INLINE SIMDVectorDouble FrameDoubleResolveTangent(const SIMDVectorDouble normal, const SIMDVectorDouble tangent, const SIMDVectorDouble fallbackTangent)noexcept{
    const SIMDVectorDouble safeFallbackTangent = FrameDoubleFallbackTangent(normal);

    SIMDVectorDouble projectedTangent = VectorDouble3IsFinite(tangent) ? FrameDoubleProjectOntoPlane(tangent, normal) : safeFallbackTangent;
    if(!FrameDoubleValidDirection(projectedTangent)){
        projectedTangent = VectorDouble3IsFinite(fallbackTangent) ? FrameDoubleProjectOntoPlane(fallbackTangent, normal) : safeFallbackTangent;
    }
    if(!FrameDoubleValidDirection(projectedTangent))
        return safeFallbackTangent;

    return FrameDoubleNormalizeDirection(projectedTangent, safeFallbackTangent);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle FrameResolveBitangent(const SIMDVectorSingle normal, const SIMDVectorSingle tangent, const SIMDVectorSingle fallbackBitangent)noexcept{
    const SIMDVectorSingle safeFallbackBitangent = FrameNormalizeDirection(Vector3IsFinite(fallbackBitangent) ? FrameProjectOntoPlane(fallbackBitangent, normal) : Vector3Cross(normal, FrameFallbackTangent(normal)), VectorSet(0.0f, 1.0f, 0.0f, 0.0f));

    SIMDVectorSingle bitangent = Vector3Cross(normal, tangent);
    if(!FrameValidDirection(bitangent))
        bitangent = safeFallbackBitangent;
    return FrameNormalizeDirection(bitangent, safeFallbackBitangent);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble FrameDoubleResolveBitangent(const SIMDVectorDouble normal, const SIMDVectorDouble tangent, const SIMDVectorDouble fallbackBitangent)noexcept{
    const SIMDVectorDouble safeFallbackBitangent = FrameDoubleNormalizeDirection(
        VectorDouble3IsFinite(fallbackBitangent) ? FrameDoubleProjectOntoPlane(fallbackBitangent, normal) : VectorDouble3Cross(normal, FrameDoubleFallbackTangent(normal)),
        VectorDoubleSet(0.0, 1.0, 0.0, 0.0)
    );

    SIMDVectorDouble bitangent = VectorDouble3Cross(normal, tangent);
    if(!FrameDoubleValidDirection(bitangent))
        bitangent = safeFallbackBitangent;
    return FrameDoubleNormalizeDirection(bitangent, safeFallbackBitangent);
}

NWB_INLINE void FrameOrthonormalize(SIMDVectorSingle& normal, SIMDVectorSingle& tangent, const SIMDVectorSingle fallbackNormal, const SIMDVectorSingle fallbackTangent)noexcept{
    normal = FrameNormalizeDirection(normal, FrameNormalizeDirection(fallbackNormal, VectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
    tangent = VectorSelect(FrameResolveTangent(normal, tangent, fallbackTangent), FrameTangentHandedness(tangent, fallbackTangent), s_SIMDMaskW);
}

NWB_INLINE void FrameDoubleOrthonormalize(SIMDVectorDouble& normal, SIMDVectorDouble& tangent, const SIMDVectorDouble fallbackNormal, const SIMDVectorDouble fallbackTangent)noexcept{
    normal = FrameDoubleNormalizeDirection(normal, FrameDoubleNormalizeDirection(fallbackNormal, VectorDoubleSet(0.0, 0.0, 1.0, 0.0)));
    tangent = VectorDoubleSelect(FrameDoubleResolveTangent(normal, tangent, fallbackTangent), FrameDoubleTangentHandedness(tangent, fallbackTangent), s_DoubleSIMDMaskW);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

