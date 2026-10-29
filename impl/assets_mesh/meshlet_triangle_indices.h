// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "geometry_payload.h"
#include "meshlet_ref_decode.h"
#include "meshlet_triangle_visit.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Flat positionStream-space triangle index buffer from meshlet streams (mirrors nwbMeshBuildMeshletVertex decode).
// Raw streams cover cooked + skinned instances.
[[nodiscard]] inline Expected<Vector<u32, Core::Alloc::ScratchArena>> BuildMeshletTriangleIndices(
    const Span<const MeshletDesc> meshlets,
    const Span<const MeshletLocalVertexRef> localVertexRefs,
    const Span<const u8> positionRefDeltas,
    const Span<const u8> primitiveIndices,
    const usize positionCount,
    Core::Alloc::ScratchArena& scratchArena
){
    const u8* const deltaBytes = positionRefDeltas.data();
    const usize deltaByteCount = positionRefDeltas.size();
    const usize primitiveIndexCount = primitiveIndices.size();

    Vector<u32, Core::Alloc::ScratchArena> outIndices(primitiveIndexCount, scratchArena);

    if(!ForEachMeshletTriangleCorner(meshlets, localVertexRefs, primitiveIndices, [&](const MeshletDesc& meshlet, const usize primitiveByte, const MeshletLocalVertexRef& localVertexRef)noexcept -> b8 {
        const b8 skinRequired = meshlet.skinBase != s_MeshMissingStreamIndex;
        const u32 localPositionIndex = static_cast<u32>(localVertexRef.localDeformedPosition);
        const auto positionRef = DecodeMeshletPositionRef(deltaBytes, deltaByteCount, meshlet, localPositionIndex, skinRequired);
        if(!positionRef)
            return false;
        if(static_cast<usize>(positionRef->position) >= positionCount)
            return false;

        outIndices[primitiveByte] = positionRef->position;
        return true;
    }))
        return MakeUnexpected(Failure{});
    return outIndices;
}

[[nodiscard]] inline Expected<Vector<u32, Core::Alloc::ScratchArena>> BuildMeshletTriangleIndices(const MeshGeometryPayload& payload, Core::Alloc::ScratchArena& scratchArena){
    return BuildMeshletTriangleIndices(payload.meshlets(), payload.meshletLocalVertexRefs(), payload.meshletPositionRefDeltas(), payload.meshletPrimitiveIndices(), payload.positionStream().size(), scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

