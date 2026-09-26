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
    typename PrimitiveContainer,
    typename IndexContainer
>
[[nodiscard]] bool BuildMeshletTriangleIndices(
    const MeshletContainer& meshlets,
    const LocalVertexRefContainer& localVertexRefs,
    const DeltaContainer& positionRefDeltas,
    const PrimitiveContainer& primitiveIndices,
    const usize positionCount,
    IndexContainer& outIndices
){
    const u8* const deltaBytes = positionRefDeltas.data();
    const usize deltaByteCount = positionRefDeltas.size();
    const usize primitiveIndexCount = primitiveIndices.size();

    outIndices.assign(primitiveIndexCount, 0u);

    return ForEachMeshletTriangleCorner(
        meshlets,
        localVertexRefs,
        primitiveIndices,
        [&](const MeshletDesc& meshlet, const usize primitiveByte, const MeshletLocalVertexRef& localVertexRef) -> bool {
            const bool skinRequired = meshlet.skinBase != s_MeshMissingStreamIndex;
            const u32 localPositionIndex = static_cast<u32>(localVertexRef.localDeformedPosition);
            MeshletPositionStreamRef positionRef;
            if(!DecodeMeshletPositionRef(deltaBytes, deltaByteCount, meshlet, localPositionIndex, skinRequired, positionRef))
                return false;
            if(static_cast<usize>(positionRef.position) >= positionCount)
                return false;

            outIndices[primitiveByte] = positionRef.position;
            return true;
        }
    );
}

template<typename IndexContainer>
[[nodiscard]] bool BuildMeshletTriangleIndices(const MeshGeometryPayload& payload, IndexContainer& outIndices){
    return BuildMeshletTriangleIndices(
        payload.meshlets(),
        payload.meshletLocalVertexRefs(),
        payload.meshletPositionRefDeltas(),
        payload.meshletPrimitiveIndices(),
        payload.positionStream().size(),
        outIndices
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

