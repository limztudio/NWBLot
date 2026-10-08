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
template<
    typename MeshletContainer,
    typename LocalVertexRefContainer,
    typename DeltaContainer,
    typename PrimitiveContainer
>
[[nodiscard]] Expected<Vector<u32, Core::Alloc::ScratchArena>> BuildMeshletTriangleIndices(
    Core::Alloc::ScratchArena& scratchArena,
    const MeshletContainer& meshlets,
    const LocalVertexRefContainer& localVertexRefs,
    const DeltaContainer& positionRefDeltas,
    const PrimitiveContainer& primitiveIndices,
    const usize positionCount
){
    const u8* const deltaBytes = positionRefDeltas.data();
    const usize deltaByteCount = positionRefDeltas.size();
    const usize primitiveIndexCount = primitiveIndices.size();

    Vector<u32, Core::Alloc::ScratchArena> outIndices(primitiveIndexCount, scratchArena);

    if(!ForEachMeshletTriangleCorner(
        meshlets,
        localVertexRefs,
        primitiveIndices,
        [&](const MeshletDesc& meshlet, const usize primitiveByte, const MeshletLocalVertexRef& localVertexRef) -> bool {
            const bool skinRequired = meshlet.skinBase != s_MeshMissingStreamIndex;
            const u32 localPositionIndex = static_cast<u32>(localVertexRef.localDeformedPosition);
            const auto positionRef = DecodeMeshletPositionRef(deltaBytes, deltaByteCount, meshlet, localPositionIndex, skinRequired);
            if(!positionRef)
                return false;
            if(static_cast<usize>(positionRef->position) >= positionCount)
                return false;

            outIndices[primitiveByte] = positionRef->position;
            return true;
        }
    ))
        return MakeUnexpected(Failure{});
    return outIndices;
}

[[nodiscard]] inline Expected<Vector<u32, Core::Alloc::ScratchArena>> BuildMeshletTriangleIndices(
    Core::Alloc::ScratchArena& scratchArena,
    const MeshGeometryPayload& payload
){
    return BuildMeshletTriangleIndices(
        scratchArena,
        payload.meshlets(),
        payload.meshletLocalVertexRefs(),
        payload.meshletPositionRefDeltas(),
        payload.meshletPrimitiveIndices(),
        payload.positionStream().size()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

