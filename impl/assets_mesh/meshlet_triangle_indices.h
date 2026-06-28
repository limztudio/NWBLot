// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "geometry_payload.h"
#include "meshlet_ref_decode.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Reconstructs a flat positionStream-space triangle index buffer (u32, 3 per triangle) from the meshlet-packed
// streams. Decode chain mirrors nwbMeshBuildMeshletVertex: primitive u8 -> meshletLocalVertexRef.localDeformedPosition
// -> DecodeMeshletPositionRef -> global position index. Output count/order equals the meshlet primitive index count.
// Takes the raw streams so it works for both the cooked asset payload and the runtime/skinned mesh instance.
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
    const usize localVertexRefCount = localVertexRefs.size();
    const usize primitiveIndexCount = primitiveIndices.size();

    outIndices.assign(primitiveIndexCount, 0u);

    for(usize meshletIndex = 0u; meshletIndex < meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = meshlets[meshletIndex];
        const u32 primitiveCount = MeshletPrimitiveCount(meshlet);
        const bool skinRequired = meshlet.skinBase != s_MeshMissingStreamIndex;

        for(u32 primitive = 0u; primitive < primitiveCount; ++primitive){
            const usize primitiveBase = static_cast<usize>(meshlet.primitiveOffset) + static_cast<usize>(primitive) * NWB_MESHLET_TRIANGLE_INDEX_COUNT;

            for(u32 corner = 0u; corner < NWB_MESHLET_TRIANGLE_INDEX_COUNT; ++corner){
                const usize primitiveByte = primitiveBase + corner;
                if(primitiveByte >= primitiveIndexCount)
                    return false;

                const u32 localVertexIndex = static_cast<u32>(primitiveIndices[primitiveByte]);
                const usize localVertexRefIndex = static_cast<usize>(meshlet.localVertexOffset) + localVertexIndex;
                if(localVertexRefIndex >= localVertexRefCount)
                    return false;

                const u32 localPositionIndex = static_cast<u32>(localVertexRefs[localVertexRefIndex].localDeformedPosition);
                MeshletPositionStreamRef positionRef;
                if(!DecodeMeshletPositionRef(deltaBytes, deltaByteCount, meshlet, localPositionIndex, skinRequired, positionRef))
                    return false;
                if(static_cast<usize>(positionRef.position) >= positionCount)
                    return false;

                outIndices[primitiveByte] = positionRef.position;
            }
        }
    }

    return true;
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

