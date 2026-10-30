// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "meshlet_constants.h"
#include <impl/assets_mesh/payload_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE u32 PackMeshletConeCutoffUnorm8(const SIMDVectorSingle value)noexcept{
    return static_cast<u32>(VectorGetX(VectorScale(VectorSaturate(value), s_MeshletUnorm8Max)));
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle FoldMeshletConeOctAxis(SIMDVectorSingle axis)noexcept{
    const SIMDVectorSingle absXY = VectorAndInt(VectorAbs(axis), s_SIMDMaskXY);
    const SIMDVectorSingle foldedMagnitude = VectorSubtract(s_SIMDOne, VectorSwizzle<1, 0, 2, 3>(absXY));
    const SIMDVectorSingle foldedSign = VectorSelect(s_SIMDNegativeOne, s_SIMDOne, VectorGreaterOrEqual(axis, VectorZero()));
    const SIMDVectorSingle foldedXY = VectorMultiply(foldedMagnitude, foldedSign);
    const SIMDVectorSingle folded = VectorSelect(axis, foldedXY, s_SIMDMaskXY);
    return VectorSelect(axis, folded, VectorLess(VectorSplatZ(axis), VectorZero()));
}

[[nodiscard]] NWB_INLINE u32 PackMeshletConeOct16(const SIMDVectorSingle axis)noexcept{
    SIMDVectorSingle octAxis = VectorSetW(axis, 0.0f);
    const SIMDVectorSingle lengthVector = VectorSum(VectorAbs(octAxis));
    if(!VectorIsFinite(lengthVector, VectorComponentMask::s_XYZW) || !Vector4Greater(lengthVector, VectorReplicate(s_MeshletConeAxisLengthEpsilon)))
        return s_MeshletConeAxisFallback;

    octAxis = FoldMeshletConeOctAxis(VectorMultiply(octAxis, VectorReciprocal(lengthVector)));
    octAxis = VectorMultiplyAddExpression(octAxis, VectorReplicate(s_MeshletOctEncodeBias), VectorReplicate(s_MeshletOctEncodeBias));

    const SIMDVectorSingle quantized = VectorMultiplyAddExpression(VectorSaturate(octAxis), VectorReplicate(s_MeshletUnorm8Max), VectorReplicate(s_MeshletUnorm8RoundingBias));
    return (static_cast<u32>(VectorGetX(quantized)) << s_MeshletConeAxisXShift) | (static_cast<u32>(VectorGetY(quantized)) << s_MeshletConeAxisYShift);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle UnpackMeshletConeOct16Axis(const u32 conePacked)noexcept{
    const SIMDVectorSingle packed = VectorSet(static_cast<f32>((conePacked >> s_MeshletConeAxisXShift) & s_MeshletPackedByteMask), static_cast<f32>((conePacked >> s_MeshletConeAxisYShift) & s_MeshletPackedByteMask), 0.0f, 0.0f);
    SIMDVectorSingle axis = VectorScale(packed, 1.0f / s_MeshletUnorm8Max);
    axis = VectorAndInt(VectorMultiplyAddExpression(axis, VectorReplicate(s_MeshletOctDecodeScale), VectorReplicate(-s_MeshletOctDecodeBias)), s_SIMDMaskXY);
    axis = VectorSelect(axis, VectorSubtract(s_SIMDOne, VectorSum(VectorAbs(axis))), s_SIMDMaskZ);
    axis = FoldMeshletConeOctAxis(axis);
    return Vector3NormalizeOr(axis, VectorSet(0.0f, 0.0f, 1.0f, 0.0f), s_MeshletConeAxisLengthSquaredEpsilon);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle ConservativePackedMeshletConeCutoff(const SIMDVectorSingle axis, const SIMDVectorSingle cutoff, const SIMDVectorSingle unpackedAxis)noexcept{
    const SIMDVectorSingle normalizedAxis = Vector3NormalizeOr(axis, unpackedAxis, s_MeshletConeAxisLengthSquaredEpsilon);
    const SIMDVectorSingle axisDot = VectorClamp(Vector3Dot(normalizedAxis, unpackedAxis), s_SIMDNegativeOne, s_SIMDOne);
    const SIMDVectorSingle safeCutoff = VectorSaturate(cutoff);
    const SIMDVectorSingle cosineTerms = VectorMergeX(safeCutoff, axisDot, VectorZero(), VectorZero());
    const SIMDVectorSingle sineTerms = VectorSqrt(VectorMax(VectorZero(), VectorSubtract(s_SIMDOne, VectorMultiply(cosineTerms, cosineTerms))));
    const SIMDVectorSingle conservativeCutoff = VectorSubtract(VectorMultiply(safeCutoff, axisDot), VectorMultiply(VectorSplatX(sineTerms), VectorSplatY(sineTerms)));
    return conservativeCutoff;
}

[[nodiscard]] NWB_INLINE u32 PackMeshletCone(const SIMDVectorSingle axis, const SIMDVectorSingle cutoff)noexcept{
    if(VectorGetX(cutoff) <= 0.0f)
        return 0u;

    const u32 packedAxis = PackMeshletConeOct16(axis);
    const u32 packedCutoff = PackMeshletConeCutoffUnorm8(ConservativePackedMeshletConeCutoff(axis, cutoff, UnpackMeshletConeOct16Axis(packedAxis)));
    if(packedCutoff == 0u)
        return 0u;

    return packedAxis | (packedCutoff << s_MeshletConeCutoffShift) | (s_MeshletConeFlagEnabled << s_MeshletConeFlagShift);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

