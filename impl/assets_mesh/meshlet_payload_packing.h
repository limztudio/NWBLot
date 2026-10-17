// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "payload_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_MeshletCountMask = NWB_MESHLET_COUNT_MASK;
inline constexpr u32 s_MeshletPrimitiveCountShift = NWB_MESHLET_PRIMITIVE_COUNT_SHIFT;
inline constexpr u32 s_MeshletPositionCountShift = NWB_MESHLET_POSITION_COUNT_SHIFT;
inline constexpr u32 s_MeshletAttributeCountShift = NWB_MESHLET_ATTRIBUTE_COUNT_SHIFT;
inline constexpr u32 s_MeshletConeAxisXShift = NWB_MESHLET_CONE_AXIS_X_SHIFT;
inline constexpr u32 s_MeshletConeAxisYShift = NWB_MESHLET_CONE_AXIS_Y_SHIFT;
inline constexpr u32 s_MeshletConeCutoffShift = NWB_MESHLET_CONE_CUTOFF_SHIFT;
inline constexpr u32 s_MeshletConeFlagShift = NWB_MESHLET_CONE_FLAG_SHIFT;
inline constexpr u32 s_MeshletPackedByteBits = NWB_MESHLET_PACKED_BYTE_BITS;
inline constexpr u32 s_MeshletPackedByteMask = NWB_MESHLET_PACKED_BYTE_MASK;
inline constexpr u32 s_MeshletConeAxisFallback = NWB_MESHLET_CONE_AXIS_FALLBACK;
inline constexpr f32 s_MeshletOctEncodeBias = 0.5f;
inline constexpr f32 s_MeshletOctDecodeScale = 2.0f;
inline constexpr f32 s_MeshletOctDecodeBias = 1.0f;
inline constexpr f32 s_MeshletConeAxisLengthEpsilon = static_cast<f32>(NWB_MESHLET_CONE_AXIS_LENGTH_EPSILON);
inline constexpr f32 s_MeshletConeAxisLengthSquaredEpsilon = static_cast<f32>(NWB_MESHLET_CONE_AXIS_LENGTH_SQUARED_EPSILON);
inline constexpr f32 s_MeshletUnorm8Max = static_cast<f32>(NWB_MESHLET_UNORM8_MAX);
inline constexpr f32 s_MeshletUnorm8RoundingBias = static_cast<f32>(NWB_MESHLET_UNORM8_ROUNDING_BIAS);
inline constexpr u32 s_MeshletRefEncodingWidthMask = NWB_MESHLET_REF_ENCODING_WIDTH_MASK;
inline constexpr u32 s_MeshletRefEncodingPositionShift = NWB_MESHLET_REF_ENCODING_POSITION_SHIFT;
inline constexpr u32 s_MeshletRefEncodingSkinShift = NWB_MESHLET_REF_ENCODING_SKIN_SHIFT;
inline constexpr u32 s_MeshletRefEncodingNormalShift = NWB_MESHLET_REF_ENCODING_NORMAL_SHIFT;
inline constexpr u32 s_MeshletRefEncodingTangentShift = NWB_MESHLET_REF_ENCODING_TANGENT_SHIFT;
inline constexpr u32 s_MeshletRefEncodingUv0Shift = NWB_MESHLET_REF_ENCODING_UV0_SHIFT;
inline constexpr u32 s_MeshletRefEncodingColorShift = NWB_MESHLET_REF_ENCODING_COLOR_SHIFT;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshletRefDeltaWidth{
    enum Enum : u32{
        U8 = NWB_MESHLET_REF_WIDTH_U8,
        U16 = NWB_MESHLET_REF_WIDTH_U16,
        U32 = NWB_MESHLET_REF_WIDTH_U32,
    };
};

[[nodiscard]] constexpr NWB_INLINE u32 PackMeshletCounts(const u32 vertexCount, const u32 primitiveCount, const u32 positionCount, const u32 attributeCount)noexcept{
    return
        (vertexCount & s_MeshletCountMask)
        | ((primitiveCount & s_MeshletCountMask) << s_MeshletPrimitiveCountShift)
        | ((positionCount & s_MeshletCountMask) << s_MeshletPositionCountShift)
        | ((attributeCount & s_MeshletCountMask) << s_MeshletAttributeCountShift)
    ;
}

[[nodiscard]] constexpr NWB_INLINE u32 MeshletVertexCount(const MeshletDesc& meshlet)noexcept{
    return meshlet.counts & s_MeshletCountMask;
}

[[nodiscard]] constexpr NWB_INLINE u32 MeshletPrimitiveCount(const MeshletDesc& meshlet)noexcept{
    return (meshlet.counts >> s_MeshletPrimitiveCountShift) & s_MeshletCountMask;
}

[[nodiscard]] constexpr NWB_INLINE u32 MeshletPositionCount(const MeshletDesc& meshlet)noexcept{
    return (meshlet.counts >> s_MeshletPositionCountShift) & s_MeshletCountMask;
}

[[nodiscard]] constexpr NWB_INLINE u32 MeshletAttributeCount(const MeshletDesc& meshlet)noexcept{
    return meshlet.counts >> s_MeshletAttributeCountShift;
}

[[nodiscard]] constexpr NWB_INLINE MeshletRefDeltaWidth::Enum MeshletRefDeltaWidthForMaxDelta(const u32 maxDelta)noexcept{
    return maxDelta <= static_cast<u32>(Limit<u8>::s_Max) ? MeshletRefDeltaWidth::U8 : maxDelta <= static_cast<u32>(Limit<u16>::s_Max) ? MeshletRefDeltaWidth::U16 : MeshletRefDeltaWidth::U32;
}

[[nodiscard]] constexpr NWB_INLINE u32 PackMeshletRefEncodingWidth(const MeshletRefDeltaWidth::Enum width, const u32 shift)noexcept{
    return (static_cast<u32>(width) & s_MeshletRefEncodingWidthMask) << shift;
}

[[nodiscard]] constexpr NWB_INLINE u32 PackMeshletRefEncoding(
    const MeshletRefDeltaWidth::Enum positionWidth,
    const MeshletRefDeltaWidth::Enum skinWidth,
    const MeshletRefDeltaWidth::Enum normalWidth,
    const MeshletRefDeltaWidth::Enum tangentWidth,
    const MeshletRefDeltaWidth::Enum uv0Width,
    const MeshletRefDeltaWidth::Enum colorWidth
)noexcept{
    return
        PackMeshletRefEncodingWidth(positionWidth, s_MeshletRefEncodingPositionShift)
        | PackMeshletRefEncodingWidth(skinWidth, s_MeshletRefEncodingSkinShift)
        | PackMeshletRefEncodingWidth(normalWidth, s_MeshletRefEncodingNormalShift)
        | PackMeshletRefEncodingWidth(tangentWidth, s_MeshletRefEncodingTangentShift)
        | PackMeshletRefEncodingWidth(uv0Width, s_MeshletRefEncodingUv0Shift)
        | PackMeshletRefEncodingWidth(colorWidth, s_MeshletRefEncodingColorShift)
    ;
}

[[nodiscard]] constexpr NWB_INLINE MeshletRefDeltaWidth::Enum MeshletRefEncodingWidth(const u32 encoding, const u32 shift)noexcept{
    return static_cast<MeshletRefDeltaWidth::Enum>((encoding >> shift) & s_MeshletRefEncodingWidthMask);
}

[[nodiscard]] constexpr NWB_INLINE bool MeshletRefDeltaFitsWidth(const u32 delta, const MeshletRefDeltaWidth::Enum width)noexcept{
    return width == MeshletRefDeltaWidth::U32 || (width == MeshletRefDeltaWidth::U16 && delta <= static_cast<u32>(Limit<u16>::s_Max)) || (width == MeshletRefDeltaWidth::U8 && delta <= static_cast<u32>(Limit<u8>::s_Max));
}

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

[[nodiscard]] NWB_INLINE u32 MeshletConeFlags(const MeshletBounds& bounds)noexcept{
    return bounds.conePacked >> s_MeshletConeFlagShift;
}

[[nodiscard]] NWB_INLINE u32 MeshletConePackedCutoff(const MeshletBounds& bounds)noexcept{
    return (bounds.conePacked >> s_MeshletConeCutoffShift) & s_MeshletPackedByteMask;
}

[[nodiscard]] NWB_INLINE bool MeshletConeEnabled(const MeshletBounds& bounds)noexcept{
    return (MeshletConeFlags(bounds) & s_MeshletConeFlagEnabled) != 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

