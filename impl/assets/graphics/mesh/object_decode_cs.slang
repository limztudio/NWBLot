// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_MESH_SHADER_EMULATION_COMPUTE 1
#define NWB_MESH_EMULATION_INDEXED_OUTPUT 1
#define NWB_CSG_ENABLED 0

#include "runtime.slangi"
#include "emulation_output.slangi"
#include "object_geometry_constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbMeshObjectVertex{
    float4 position;
    uint2 normal;
    uint2 tangent;
    float2 uv0;
    uint2 color;
};

#pragma warning(push)
#pragma warning(disable: 39001) // The object cache shares the canonical StorageBuffer descriptor table.
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_STORAGE_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
RWStructuredBuffer<NwbMeshObjectVertex, Std430DataLayout> g_NwbHeapMeshObjectVertexBuffers[];
#pragma warning(pop)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_MESH_SHADER_GROUP_SIZE_X, 1, 1)]
void main(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint meshletIndex = groupID.x;
    const uint outputSlot = nwbMeshFrameGeneratedVertexHeapSlot();
    // The marker makes the culled vertex independent of the current instance and view, including an empty mesh.
    if(meshletIndex == 0u && groupThreadID.x == 0u){
        const NwbMeshObjectVertex sentinel = { float4(0.0), uint2(0u), uint2(0u), float2(0.0), uint2(0u) };
        g_NwbHeapMeshObjectVertexBuffers[outputSlot][NWB_MESH_OBJECT_SENTINEL_VERTEX_INDEX] = sentinel;
    }
    if(meshletIndex >= nwbMeshletCount())
        return;

    nwbMeshLoadGeometryHeapSlots(groupThreadID.x);
    const NwbMeshletDesc meshlet = NwbHeapMeshletDescs(nwbMeshGeometryHeapSlot(NWB_MESH_BINDING_MESHLET_DESC))[meshletIndex];
    const NwbMeshletRefDecodeInfo decodeInfo = nwbMeshletBuildRefDecodeInfo(meshlet);
    const uint vertexCount = nwbMeshletVertexCount(meshlet);
    for(uint vertexIndex = groupThreadID.x; vertexIndex < vertexCount; vertexIndex += NWB_MESH_SHADER_GROUP_SIZE_X){
        const NwbMeshletLocalVertexRef localRef = nwbMeshLoadMeshletLocalVertexRef(meshlet, vertexIndex);
        const NwbMeshletPositionStreamRef positionRef = nwbMeshLoadMeshletPositionRef(
            meshlet, decodeInfo, nwbMeshletLocalDeformedPosition(localRef)
        );
        const float3 objectPosition = nwbMeshLoadPosition(
            NwbHeapMeshPositions(nwbMeshGeometryHeapSlot(NWB_MESH_BINDING_POSITION))[positionRef.position]
        );
        const NwbMeshSourceVertex source = nwbMeshLoadSourceVertex(
            meshlet, decodeInfo, vertexIndex, objectPosition, float3(0.0), float4(0.0)
        );
        NwbMeshObjectVertex output;
        output.position = float4(objectPosition, 1.0);
        output.normal = nwbMeshPackGeneratedHalf4(half4(half3(source.normal), half(0.0)));
        output.tangent = nwbMeshPackGeneratedHalf4(half4(source.tangent));
        output.uv0 = source.uv0;
        output.color = nwbMeshPackGeneratedHalf4(source.color);
        const uint outputIndex = NWB_MESH_OBJECT_FIRST_VERTEX_INDEX + meshlet.localVertexOffset + vertexIndex;
        g_NwbHeapMeshObjectVertexBuffers[outputSlot][outputIndex] = output;
    }

    // Topology is invariant under instance/view changes. Preserve primitive offsets, winding, and local-reference seams.
    const uint primitiveCount = nwbMeshletPrimitiveCount(meshlet);
    for(uint primitiveIndex = groupThreadID.x; primitiveIndex < primitiveCount; primitiveIndex += NWB_MESH_SHADER_GROUP_SIZE_X){
        const uint primitiveBase = meshlet.primitiveOffset + primitiveIndex * NWB_MESHLET_TRIANGLE_INDEX_COUNT;
        const uint3 localIndices = uint3(
            nwbMeshLoadMeshletPrimitiveIndex(primitiveBase),
            nwbMeshLoadMeshletPrimitiveIndex(primitiveBase + 1u),
            nwbMeshLoadMeshletPrimitiveIndex(primitiveBase + 2u)
        );
        const uint3 indices = localIndices + (NWB_MESH_OBJECT_FIRST_VERTEX_INDEX + meshlet.localVertexOffset);
        nwbMeshStoreGeneratedIndices(primitiveBase, indices);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

