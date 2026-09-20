// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_MESH_SHADER_EMULATION_COMPUTE 1
#define NWB_MESH_EMULATION_INDEXED_OUTPUT 1
#define NWB_CSG_ENABLED 0

#include "runtime.slangi"
#include "encoding.slangi"
#include "culling.slangi"
#include "emulation_output.slangi"
#include "object_geometry_constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


groupshared float4 g_NwbMeshObjectClipPositions[NWB_MESH_SHADER_MAX_VERTICES];


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float4 nwbMeshObjectPrimitiveClipPosition(const NwbMeshletDesc meshlet, const uint localVertexIndex){
    const NwbMeshletLocalVertexRef localRef = nwbMeshLoadMeshletLocalVertexRef(meshlet, localVertexIndex);
    return g_NwbMeshObjectClipPositions[nwbMeshletLocalDeformedPosition(localRef)];
}

[numthreads(NWB_MESH_SHADER_GROUP_SIZE_X, 1, 1)]
void main(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint meshletIndex = groupID.x;
    if(meshletIndex >= nwbMeshletCount())
        return;

    nwbMeshLoadGeometryHeapSlots(groupThreadID.x);
    const NwbMeshletDesc meshlet = NwbHeapMeshletDescs(nwbMeshGeometryHeapSlot(NWB_MESH_BINDING_MESHLET_DESC))[meshletIndex];
    const NwbMeshletRefDecodeInfo decodeInfo = nwbMeshletBuildRefDecodeInfo(meshlet);
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const uint primitiveCount = nwbMeshletPrimitiveCount(meshlet);
    if(nwbMeshletCulled(meshletIndex, instance)){
        for(uint primitiveIndex = groupThreadID.x; primitiveIndex < primitiveCount; primitiveIndex += NWB_MESH_SHADER_GROUP_SIZE_X){
            const uint primitiveBase = meshlet.primitiveOffset + primitiveIndex * NWB_MESHLET_TRIANGLE_INDEX_COUNT;
            nwbMeshStoreGeneratedIndices(primitiveBase, uint3(NWB_MESH_OBJECT_SENTINEL_VERTEX_INDEX));
        }
        return;
    }

    const uint positionCount = nwbMeshletPositionCount(meshlet);
    for(uint positionIndex = groupThreadID.x; positionIndex < positionCount; positionIndex += NWB_MESH_SHADER_GROUP_SIZE_X){
        const NwbMeshletPositionStreamRef positionRef = nwbMeshLoadMeshletPositionRef(meshlet, decodeInfo, positionIndex);
        const float3 objectPosition = nwbMeshLoadPosition(
            NwbHeapMeshPositions(nwbMeshGeometryHeapSlot(NWB_MESH_BINDING_POSITION))[positionRef.position]
        );
        g_NwbMeshObjectClipPositions[positionIndex] = nwbMeshTransformWorldToClip(nwbMeshTransformPosition(objectPosition, instance));
    }
    GroupMemoryBarrierWithGroupSync();

    for(uint primitiveIndex = groupThreadID.x; primitiveIndex < primitiveCount; primitiveIndex += NWB_MESH_SHADER_GROUP_SIZE_X){
        const uint primitiveBase = meshlet.primitiveOffset + primitiveIndex * NWB_MESHLET_TRIANGLE_INDEX_COUNT;
        const uint3 localIndices = uint3(
            nwbMeshLoadMeshletPrimitiveIndex(primitiveBase),
            nwbMeshLoadMeshletPrimitiveIndex(primitiveBase + 1u),
            nwbMeshLoadMeshletPrimitiveIndex(primitiveBase + 2u)
        );
        const float4 a = nwbMeshObjectPrimitiveClipPosition(meshlet, localIndices.x);
        const float4 b = nwbMeshObjectPrimitiveClipPosition(meshlet, localIndices.y);
        const float4 c = nwbMeshObjectPrimitiveClipPosition(meshlet, localIndices.z);
        const bool culled = nwbMeshAllOutsideClipPlane(a, b, c) || nwbMeshAllOutsideScissor(a, b, c);
        const uint3 outputIndices = culled
            ? uint3(NWB_MESH_OBJECT_SENTINEL_VERTEX_INDEX)
            : localIndices + (NWB_MESH_OBJECT_FIRST_VERTEX_INDEX + meshlet.localVertexOffset)
        ;
        nwbMeshStoreGeneratedIndices(primitiveBase, outputIndices);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

