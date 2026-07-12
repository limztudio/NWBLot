
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
inline constexpr u32 s_MeshletPackedHalfwordBits = NWB_MESHLET_PACKED_HALFWORD_BITS;
inline constexpr u32 s_MeshletPackedHalfwordMask = NWB_MESHLET_PACKED_HALFWORD_MASK;
inline constexpr u32 s_MeshletConeAxisFallback = NWB_MESHLET_CONE_AXIS_FALLBACK;
inline constexpr f32 s_MeshletConeAxisLengthEpsilon = static_cast<f32>(NWB_MESHLET_CONE_AXIS_LENGTH_EPSILON);
inline constexpr f32 s_MeshletConeAxisLengthSquaredEpsilon = static_cast<f32>(NWB_MESHLET_CONE_AXIS_LENGTH_SQUARED_EPSILON);
inline constexpr f32 s_MeshletUnorm8Max = static_cast<f32>(Limit<u8>::s_Max);
inline constexpr f32 s_MeshletUnorm8RoundingBias = 0.5f;
inline constexpr u32 s_MeshletRefEncodingWidthMask = NWB_MESHLET_REF_ENCODING_WIDTH_MASK;
inline constexpr u32 s_MeshletRefEncodingPositionShift = NWB_MESHLET_REF_ENCODING_POSITION_SHIFT;
inline constexpr u32 s_MeshletRefEncodingSkinShift = NWB_MESHLET_REF_ENCODING_SKIN_SHIFT;
inline constexpr u32 s_MeshletRefEncodingNormalShift = NWB_MESHLET_REF_ENCODING_NORMAL_SHIFT;
inline constexpr u32 s_MeshletRefEncodingTangentShift = NWB_MESHLET_REF_ENCODING_TANGENT_SHIFT;
inline constexpr u32 s_MeshletRefEncodingUv0Shift = NWB_MESHLET_REF_ENCODING_UV0_SHIFT;
inline constexpr u32 s_MeshletRefEncodingColorShift = NWB_MESHLET_REF_ENCODING_COLOR_SHIFT;

namespace MeshletRefDeltaWidth{
    enum Enum : u32{
        U8 = NWB_MESHLET_REF_WIDTH_U8,
        U16 = NWB_MESHLET_REF_WIDTH_U16,
        U32 = NWB_MESHLET_REF_WIDTH_U32,
    };
};

[[nodiscard]] constexpr NWB_INLINE u32 PackMeshletCounts(
    const u32 vertexCount,
    const u32 primitiveCount,
    const u32 positionCount,
    const u32 attributeCount
){
    return (vertexCount & s_MeshletCountMask)
        | ((primitiveCount & s_MeshletCountMask) << s_MeshletPrimitiveCountShift)
        | ((positionCount & s_MeshletCountMask) << s_MeshletPositionCountShift)
        | ((attributeCount & s_MeshletCountMask) << s_MeshletAttributeCountShift)
    ;
}

[[nodiscard]] constexpr NWB_INLINE u32 MeshletVertexCount(const MeshletDesc& meshlet){
    return meshlet.counts & s_MeshletCountMask;
}

[[nodiscard]] constexpr NWB_INLINE u32 MeshletPrimitiveCount(const MeshletDesc& meshlet){
    return (meshlet.counts >> s_MeshletPrimitiveCountShift) & s_MeshletCountMask;
}

[[nodiscard]] constexpr NWB_INLINE u32 MeshletPositionCount(const MeshletDesc& meshlet){
    return (meshlet.counts >> s_MeshletPositionCountShift) & s_MeshletCountMask;
}

[[nodiscard]] constexpr NWB_INLINE u32 MeshletAttributeCount(const MeshletDesc& meshlet){
    return meshlet.counts >> s_MeshletAttributeCountShift;
}

[[nodiscard]] constexpr NWB_INLINE MeshletRefDeltaWidth::Enum MeshletRefDeltaWidthForMaxDelta(const u32 maxDelta){
    return maxDelta <= static_cast<u32>(Limit<u8>::s_Max)
        ? MeshletRefDeltaWidth::U8
        : maxDelta <= static_cast<u32>(Limit<u16>::s_Max)
            ? MeshletRefDeltaWidth::U16
            : MeshletRefDeltaWidth::U32
    ;
}

[[nodiscard]] constexpr NWB_INLINE u32 PackMeshletRefEncodingWidth(
    const MeshletRefDeltaWidth::Enum width,
    const u32 shift
){
    return (static_cast<u32>(width) & s_MeshletRefEncodingWidthMask) << shift;
}

[[nodiscard]] constexpr NWB_INLINE u32 PackMeshletRefEncoding(
    const MeshletRefDeltaWidth::Enum positionWidth,
    const MeshletRefDeltaWidth::Enum skinWidth,
    const MeshletRefDeltaWidth::Enum normalWidth,
    const MeshletRefDeltaWidth::Enum tangentWidth,
    const MeshletRefDeltaWidth::Enum uv0Width,
    const MeshletRefDeltaWidth::Enum colorWidth
){
    return
        PackMeshletRefEncodingWidth(positionWidth, s_MeshletRefEncodingPositionShift)
        | PackMeshletRefEncodingWidth(skinWidth, s_MeshletRefEncodingSkinShift)
        | PackMeshletRefEncodingWidth(normalWidth, s_MeshletRefEncodingNormalShift)
        | PackMeshletRefEncodingWidth(tangentWidth, s_MeshletRefEncodingTangentShift)
        | PackMeshletRefEncodingWidth(uv0Width, s_MeshletRefEncodingUv0Shift)
        | PackMeshletRefEncodingWidth(colorWidth, s_MeshletRefEncodingColorShift)
    ;
}

[[nodiscard]] constexpr NWB_INLINE MeshletRefDeltaWidth::Enum MeshletRefEncodingWidth(
    const u32 encoding,
    const u32 shift
){
    return static_cast<MeshletRefDeltaWidth::Enum>((encoding >> shift) & s_MeshletRefEncodingWidthMask);
}

[[nodiscard]] constexpr NWB_INLINE bool MeshletRefDeltaFitsWidth(
    const u32 delta,
    const MeshletRefDeltaWidth::Enum width
){
    return width == MeshletRefDeltaWidth::U32
        || (width == MeshletRefDeltaWidth::U16 && delta <= static_cast<u32>(Limit<u16>::s_Max))
        || (width == MeshletRefDeltaWidth::U8 && delta <= static_cast<u32>(Limit<u8>::s_Max))
    ;
}

[[nodiscard]] NWB_INLINE u32 PackMeshletConeUnorm8(const f32 value){
    return static_cast<u32>(Saturate(value) * s_MeshletUnorm8Max + s_MeshletUnorm8RoundingBias);
}

[[nodiscard]] NWB_INLINE u32 PackMeshletConeCutoffUnorm8(const f32 value){
    return static_cast<u32>(Saturate(value) * s_MeshletUnorm8Max);
}

[[nodiscard]] NWB_INLINE SIMDVector FoldMeshletConeOctAxis(SIMDVector axis){
    const SIMDVector absXY = VectorAndInt(VectorAbs(axis), s_SIMDMaskXY);
    const SIMDVector foldedMagnitude = VectorSubtract(s_SIMDOne, VectorSwizzle<1, 0, 2, 3>(absXY));
    const SIMDVector foldedSign = VectorSelect(
        s_SIMDNegativeOne,
        s_SIMDOne,
        VectorGreaterOrEqual(axis, VectorZero())
    );
    const SIMDVector foldedXY = VectorMultiply(foldedMagnitude, foldedSign);
    const SIMDVector folded = VectorSelect(axis, foldedXY, s_SIMDMaskXY);
    return VectorSelect(axis, folded, VectorLess(VectorSplatZ(axis), VectorZero()));
}

[[nodiscard]] NWB_INLINE u32 PackMeshletConeOct16(const SIMDVector axis){
    SIMDVector octAxis = VectorSetW(axis, 0.0f);
    const SIMDVector lengthVector = VectorSum(VectorAbs(octAxis));
    if(!VectorIsFinite(lengthVector, 0xFu) || !Vector4Greater(lengthVector, VectorReplicate(s_MeshletConeAxisLengthEpsilon)))
        return s_MeshletConeAxisFallback;

    octAxis = FoldMeshletConeOctAxis(VectorMultiply(octAxis, VectorReciprocal(lengthVector)));

    return
        (PackMeshletConeUnorm8(VectorGetX(octAxis) * 0.5f + 0.5f) << s_MeshletConeAxisXShift)
        | (PackMeshletConeUnorm8(VectorGetY(octAxis) * 0.5f + 0.5f) << s_MeshletConeAxisYShift)
    ;
}

[[nodiscard]] NWB_INLINE f32 UnpackMeshletConeUnorm8(const u32 value, const u32 bitShift){
    return static_cast<f32>((value >> bitShift) & s_MeshletPackedByteMask) * (1.0f / s_MeshletUnorm8Max);
}

[[nodiscard]] NWB_INLINE SIMDVector UnpackMeshletConeOct16Axis(const u32 conePacked){
    SIMDVector axis = VectorSet(
        UnpackMeshletConeUnorm8(conePacked, s_MeshletConeAxisXShift) * 2.0f - 1.0f,
        UnpackMeshletConeUnorm8(conePacked, s_MeshletConeAxisYShift) * 2.0f - 1.0f,
        0.0f,
        0.0f
    );
    axis = VectorSelect(axis, VectorSubtract(s_SIMDOne, VectorSum(VectorAbs(axis))), s_SIMDMaskZ);
    axis = FoldMeshletConeOctAxis(axis);
    return Vector3NormalizeOr(
        axis,
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f),
        s_MeshletConeAxisLengthSquaredEpsilon
    );
}

[[nodiscard]] NWB_INLINE f32 ConservativePackedMeshletConeCutoff(const SIMDVector axis, const f32 cutoff, const u32 packedAxis){
    const SIMDVector unpackedAxis = UnpackMeshletConeOct16Axis(packedAxis);
    const SIMDVector normalizedAxis = Vector3NormalizeOr(
        axis,
        unpackedAxis,
        s_MeshletConeAxisLengthSquaredEpsilon
    );
    const SIMDVector axisDot = VectorClamp(Vector3Dot(normalizedAxis, unpackedAxis), s_SIMDNegativeOne, s_SIMDOne);
    const SIMDVector safeCutoff = VectorReplicate(Saturate(cutoff));
    const SIMDVector cosineTerms = VectorMergeX(safeCutoff, axisDot, VectorZero(), VectorZero());
    const SIMDVector sineTerms = VectorSqrt(VectorMax(
        VectorZero(),
        VectorSubtract(s_SIMDOne, VectorMultiply(cosineTerms, cosineTerms))
    ));
    const SIMDVector conservativeCutoff = VectorSubtract(
        VectorMultiply(safeCutoff, axisDot),
        VectorMultiply(VectorSplatX(sineTerms), VectorSplatY(sineTerms))
    );
    return VectorGetX(conservativeCutoff);
}

[[nodiscard]] NWB_INLINE u32 PackMeshletCone(const SIMDVector axis, const f32 cutoff){
    if(cutoff <= 0.0f)
        return 0u;

    const u32 packedAxis = PackMeshletConeOct16(axis);
    const u32 packedCutoff = PackMeshletConeCutoffUnorm8(
        ConservativePackedMeshletConeCutoff(axis, cutoff, packedAxis)
    );
    if(packedCutoff == 0u)
        return 0u;

    return
        packedAxis
        | (packedCutoff << s_MeshletConeCutoffShift)
        | (s_MeshletConeFlagEnabled << s_MeshletConeFlagShift)
    ;
}

[[nodiscard]] NWB_INLINE u32 MeshletConeFlags(const MeshletBounds& bounds){
    return bounds.conePacked >> s_MeshletConeFlagShift;
}

[[nodiscard]] NWB_INLINE u32 MeshletConePackedCutoff(const MeshletBounds& bounds){
    return (bounds.conePacked >> s_MeshletConeCutoffShift) & s_MeshletPackedByteMask;
}

[[nodiscard]] NWB_INLINE bool MeshletConeEnabled(const MeshletBounds& bounds){
    return (MeshletConeFlags(bounds) & s_MeshletConeFlagEnabled) != 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

