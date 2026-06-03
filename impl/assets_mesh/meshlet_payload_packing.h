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
inline constexpr u32 s_MeshletPackedHalfwordBits = NWB_MESHLET_PACKED_HALFWORD_BITS;
inline constexpr u32 s_MeshletPackedHalfwordMask = NWB_MESHLET_PACKED_HALFWORD_MASK;
inline constexpr u32 s_MeshletConeAxisFallback = NWB_MESHLET_CONE_AXIS_FALLBACK;
inline constexpr f32 s_MeshletConeAxisLengthEpsilon = static_cast<f32>(NWB_MESHLET_CONE_AXIS_LENGTH_EPSILON);
inline constexpr f32 s_MeshletConeAxisLengthSquaredEpsilon = static_cast<f32>(NWB_MESHLET_CONE_AXIS_LENGTH_SQUARED_EPSILON);
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

[[nodiscard]] inline constexpr u32 PackMeshletCounts(
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

[[nodiscard]] inline constexpr u32 MeshletVertexCount(const MeshletDesc& meshlet){
    return meshlet.counts & s_MeshletCountMask;
}

[[nodiscard]] inline constexpr u32 MeshletPrimitiveCount(const MeshletDesc& meshlet){
    return (meshlet.counts >> s_MeshletPrimitiveCountShift) & s_MeshletCountMask;
}

[[nodiscard]] inline constexpr u32 MeshletPositionCount(const MeshletDesc& meshlet){
    return (meshlet.counts >> s_MeshletPositionCountShift) & s_MeshletCountMask;
}

[[nodiscard]] inline constexpr u32 MeshletAttributeCount(const MeshletDesc& meshlet){
    return meshlet.counts >> s_MeshletAttributeCountShift;
}

[[nodiscard]] inline constexpr MeshletRefDeltaWidth::Enum MeshletRefDeltaWidthForMaxDelta(const u32 maxDelta){
    return maxDelta <= static_cast<u32>(Limit<u8>::s_Max)
        ? MeshletRefDeltaWidth::U8
        : maxDelta <= static_cast<u32>(Limit<u16>::s_Max)
            ? MeshletRefDeltaWidth::U16
            : MeshletRefDeltaWidth::U32
    ;
}

[[nodiscard]] inline constexpr u32 PackMeshletRefEncodingWidth(
    const MeshletRefDeltaWidth::Enum width,
    const u32 shift
){
    return (static_cast<u32>(width) & s_MeshletRefEncodingWidthMask) << shift;
}

[[nodiscard]] inline constexpr u32 PackMeshletRefEncoding(
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

[[nodiscard]] inline constexpr MeshletRefDeltaWidth::Enum MeshletRefEncodingWidth(
    const u32 encoding,
    const u32 shift
){
    return static_cast<MeshletRefDeltaWidth::Enum>((encoding >> shift) & s_MeshletRefEncodingWidthMask);
}

[[nodiscard]] inline constexpr bool MeshletRefDeltaFitsWidth(
    const u32 delta,
    const MeshletRefDeltaWidth::Enum width
){
    return width == MeshletRefDeltaWidth::U32
        || (width == MeshletRefDeltaWidth::U16 && delta <= static_cast<u32>(Limit<u16>::s_Max))
        || (width == MeshletRefDeltaWidth::U8 && delta <= static_cast<u32>(Limit<u8>::s_Max))
    ;
}

[[nodiscard]] inline u32 PackMeshletConeUnorm8(const f32 value){
    return static_cast<u32>(Saturate(value) * 255.0f + 0.5f);
}

[[nodiscard]] inline u32 PackMeshletConeCutoffUnorm8(const f32 value){
    return static_cast<u32>(Saturate(value) * 255.0f);
}

[[nodiscard]] inline u32 PackMeshletConeOct16(const SIMDVector axis){
    f32 x = VectorGetX(axis);
    f32 y = VectorGetY(axis);
    f32 z = VectorGetZ(axis);
    const f32 length = Abs(x) + Abs(y) + Abs(z);
    if(!IsFinite(length) || length <= s_MeshletConeAxisLengthEpsilon)
        return s_MeshletConeAxisFallback;

    const f32 invLength = 1.0f / length;
    x *= invLength;
    y *= invLength;
    z *= invLength;
    if(z < 0.0f){
        const f32 foldedX = (1.0f - Abs(y)) * (x < 0.0f ? -1.0f : 1.0f);
        const f32 foldedY = (1.0f - Abs(x)) * (y < 0.0f ? -1.0f : 1.0f);
        x = foldedX;
        y = foldedY;
    }

    return
        (PackMeshletConeUnorm8(x * 0.5f + 0.5f) << s_MeshletConeAxisXShift)
        | (PackMeshletConeUnorm8(y * 0.5f + 0.5f) << s_MeshletConeAxisYShift)
    ;
}

[[nodiscard]] inline f32 UnpackMeshletConeUnorm8(const u32 value, const u32 bitShift){
    return static_cast<f32>((value >> bitShift) & s_MeshletPackedByteMask) * (1.0f / 255.0f);
}

[[nodiscard]] inline SIMDVector UnpackMeshletConeOct16Axis(const u32 conePacked){
    f32 x = UnpackMeshletConeUnorm8(conePacked, s_MeshletConeAxisXShift) * 2.0f - 1.0f;
    f32 y = UnpackMeshletConeUnorm8(conePacked, s_MeshletConeAxisYShift) * 2.0f - 1.0f;
    f32 z = 1.0f - Abs(x) - Abs(y);
    if(z < 0.0f){
        const f32 foldedX = (1.0f - Abs(y)) * (x < 0.0f ? -1.0f : 1.0f);
        const f32 foldedY = (1.0f - Abs(x)) * (y < 0.0f ? -1.0f : 1.0f);
        x = foldedX;
        y = foldedY;
    }

    const SIMDVector axis = VectorSet(x, y, z, 0.0f);
    return Vector3NormalizeOr(
        axis,
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f),
        s_MeshletConeAxisLengthSquaredEpsilon
    );
}

[[nodiscard]] inline f32 ConservativePackedMeshletConeCutoff(const SIMDVector axis, const f32 cutoff, const u32 packedAxis){
    const SIMDVector unpackedAxis = UnpackMeshletConeOct16Axis(packedAxis);
    const SIMDVector normalizedAxis = Vector3NormalizeOr(
        axis,
        unpackedAxis,
        s_MeshletConeAxisLengthSquaredEpsilon
    );
    const f32 axisDot = Max(-1.0f, Min(1.0f, VectorGetX(Vector3Dot(normalizedAxis, unpackedAxis))));
    const f32 safeCutoff = Saturate(cutoff);
    const SIMDVector cosineTerms = VectorSet(safeCutoff, axisDot, 0.0f, 0.0f);
    const SIMDVector sineTerms = VectorSqrt(VectorMax(
        VectorZero(),
        VectorSubtract(s_SIMDOne, VectorMultiply(cosineTerms, cosineTerms))
    ));
    const f32 sinTheta = VectorGetX(sineTerms);
    const f32 sinAxisError = VectorGetY(sineTerms);
    return safeCutoff * axisDot - sinTheta * sinAxisError;
}

[[nodiscard]] inline u32 PackMeshletCone(const SIMDVector axis, const f32 cutoff){
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

[[nodiscard]] inline u32 MeshletConeFlags(const MeshletBounds& bounds){
    return bounds.conePacked >> s_MeshletConeFlagShift;
}

[[nodiscard]] inline u32 MeshletConePackedCutoff(const MeshletBounds& bounds){
    return (bounds.conePacked >> s_MeshletConeCutoffShift) & s_MeshletPackedByteMask;
}

[[nodiscard]] inline bool MeshletConeEnabled(const MeshletBounds& bounds){
    return (MeshletConeFlags(bounds) & s_MeshletConeFlagEnabled) != 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

