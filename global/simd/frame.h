// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector.h"

#include "../expected.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_FrameDirectionEpsilon = 0.00000001f;
static constexpr f32 s_FrameHandednessEpsilon = 0.000001f;
static constexpr f32 s_FrameFallbackTangentZAxisAlignmentThreshold = 0.999f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] FOUNDATION_INLINE b8 FrameValidDirection(const SIMDVectorSingle value)noexcept{
    return Vector3IsFinite(value) && Vector3Greater(Vector3LengthSq(value), VectorReplicate(s_FrameDirectionEpsilon));
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FrameNormalizeDirection(const SIMDVectorSingle value, const SIMDVectorSingle fallback)noexcept{
    return Vector3NormalizeOr(value, fallback, s_FrameDirectionEpsilon);
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FrameProjectOntoPlane(const SIMDVectorSingle value, const SIMDVectorSingle normal)noexcept{
    return VectorMultiplyAdd(normal, VectorNegate(Vector3Dot(value, normal)), value);
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FrameFallbackTangent(const SIMDVectorSingle normal)noexcept{
    const SIMDVectorSingle zAxis = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    const SIMDVectorSingle yAxis = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVectorSingle axis = VectorSelect(yAxis, zAxis, VectorLess(VectorAbs(VectorSplatZ(normal)), VectorReplicate(s_FrameFallbackTangentZAxisAlignmentThreshold)));
    return FrameNormalizeDirection(Vector3Cross(axis, normal), VectorSet(1.0f, 0.0f, 0.0f, 0.0f));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FrameTangentHandedness(const SIMDVectorSingle handedness, const SIMDVectorSingle fallbackHandedness)noexcept{
    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle sign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, VectorLess(handedness, zero));
    const SIMDVectorSingle fallbackSign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, VectorLess(fallbackHandedness, zero));
    return VectorSelect(fallbackSign, sign, VectorGreater(VectorAbs(handedness), VectorReplicate(s_FrameHandednessEpsilon)));
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FrameResolveTangent(const SIMDVectorSingle normal, const SIMDVectorSingle tangent, const SIMDVectorSingle fallbackTangent)noexcept{
    const SIMDVectorSingle safeFallbackTangent = FrameFallbackTangent(normal);

    SIMDVectorSingle projectedTangent = Vector3IsFinite(tangent) ? FrameProjectOntoPlane(tangent, normal) : safeFallbackTangent;
    if(!FrameValidDirection(projectedTangent)){
        projectedTangent = Vector3IsFinite(fallbackTangent) ? FrameProjectOntoPlane(fallbackTangent, normal) : safeFallbackTangent;
    }
    if(!FrameValidDirection(projectedTangent))
        return safeFallbackTangent;

    return FrameNormalizeDirection(projectedTangent, safeFallbackTangent);
}


[[nodiscard]] inline SIMDVectorSingle FrameResolveTangentHandedness(const SIMDVectorSingle tangentWithHandedness, const SIMDVectorSingle normal, const SIMDVectorSingle tangent, const SIMDVectorSingle bitangent)noexcept{
    const SIMDVectorSingle value = VectorSplatW(tangentWithHandedness);
    SIMDVectorSingle handedness = s_SIMDOne;
    if(VectorIsFinite(value, VectorComponentMask::s_XYZW)){
        const SIMDVectorSingle sign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, VectorLess(value, VectorZero()));
        handedness = VectorSelect(s_SIMDOne, sign, VectorGreater(VectorAbs(value), VectorReplicate(s_FrameHandednessEpsilon)));
    }
    if(FrameValidDirection(bitangent)){
        const SIMDVectorSingle bitangentSign = Vector3Dot(Vector3Cross(normal, tangent), bitangent);
        if(VectorIsFinite(bitangentSign, VectorComponentMask::s_XYZW) && Vector4Greater(VectorAbs(bitangentSign), VectorReplicate(s_FrameHandednessEpsilon)))
            handedness = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, VectorLess(bitangentSign, VectorZero()));
    }
    return VectorSelect(tangent, handedness, s_SIMDMaskW);
}

struct alignas(SIMDVectorSingle) TangentFrameDirections{
    SIMDVectorSingle tangent;
    SIMDVectorSingle bitangent;
};
static_assert(alignof(Expected<TangentFrameDirections>) >= alignof(TangentFrameDirections));

[[nodiscard]] inline Expected<TangentFrameDirections> FrameTriangleTangentDirections(const SIMDVectorSingle uv0, const SIMDVectorSingle uv1, const SIMDVectorSingle uv2, SIMDVectorSingle edge01, SIMDVectorSingle edge02)noexcept{
    const SIMDVectorSingle uvDelta1 = VectorSubtract(uv1, uv0);
    const SIMDVectorSingle uvDelta2 = VectorSubtract(uv2, uv0);
    const SIMDVectorSingle determinantVector = Vector2Cross(uvDelta1, uvDelta2);
    if(!VectorIsFinite(determinantVector, VectorComponentMask::s_XYZW) || Vector4LessOrEqual(VectorAbs(determinantVector), VectorReplicate(s_FrameHandednessEpsilon)))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle du1 = VectorSplatX(uvDelta1);
    const SIMDVectorSingle dv1 = VectorSplatY(uvDelta1);
    const SIMDVectorSingle du2 = VectorSplatX(uvDelta2);
    const SIMDVectorSingle dv2 = VectorSplatY(uvDelta2);
    const SIMDVectorSingle inverseDeterminant = VectorReciprocal(determinantVector);
    const SIMDVectorSingle tangent = VectorMultiply(VectorSubtract(VectorMultiply(edge01, dv2), VectorMultiply(edge02, dv1)), inverseDeterminant);
    const SIMDVectorSingle bitangent = VectorMultiply(VectorSubtract(VectorMultiply(edge02, du1), VectorMultiply(edge01, du2)), inverseDeterminant);
    if(!FrameValidDirection(tangent) || !FrameValidDirection(bitangent))
        return MakeUnexpected(Failure{});
    return TangentFrameDirections{ tangent, bitangent };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

