// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "mesh_payload_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_MeshletCountMask = 0xffu;
inline constexpr u32 s_MeshletPrimitiveCountShift = 8u;
inline constexpr u32 s_MeshletPositionCountShift = 16u;
inline constexpr u32 s_MeshletAttributeCountShift = 24u;
inline constexpr u32 s_MeshletConeAxisXShift = 0u;
inline constexpr u32 s_MeshletConeAxisYShift = 8u;
inline constexpr u32 s_MeshletConeCutoffShift = 16u;
inline constexpr u32 s_MeshletConeFlagShift = 24u;

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

[[nodiscard]] inline u32 PackMeshletConeUnorm8(const f32 value){
    return static_cast<u32>(Saturate(value) * 255.0f + 0.5f);
}

[[nodiscard]] inline u32 PackMeshletConeOct16(const SIMDVector axis){
    f32 x = VectorGetX(axis);
    f32 y = VectorGetY(axis);
    f32 z = VectorGetZ(axis);
    const f32 length = Abs(x) + Abs(y) + Abs(z);
    if(!IsFinite(length) || length <= 0.000001f)
        return 0x8080u;

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

[[nodiscard]] inline u32 PackMeshletCone(const SIMDVector axis, const f32 cutoff){
    if(cutoff <= 0.0f)
        return 0u;

    const u32 packedCutoff = PackMeshletConeUnorm8(cutoff);
    if(packedCutoff == 0u)
        return 0u;

    return PackMeshletConeOct16(axis) | (packedCutoff << s_MeshletConeCutoffShift) | (s_MeshletConeFlagEnabled << s_MeshletConeFlagShift);
}

[[nodiscard]] inline u32 MeshletConeFlags(const MeshletBounds& bounds){
    return bounds.conePacked >> s_MeshletConeFlagShift;
}

[[nodiscard]] inline u32 MeshletConePackedCutoff(const MeshletBounds& bounds){
    return (bounds.conePacked >> s_MeshletConeCutoffShift) & 0xffu;
}

[[nodiscard]] inline bool MeshletConeEnabled(const MeshletBounds& bounds){
    return (MeshletConeFlags(bounds) & s_MeshletConeFlagEnabled) != 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
