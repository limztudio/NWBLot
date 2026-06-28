// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../mesh/meshlet_payload.slangi"
#include "binding_slots.h"
#include "constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-frame skinned-normal repack. The RT attribute buffer (AttribGpu: Half4 normal @ +0, Float2 uv0 @ +8, 16-byte
// stride, triangle-corner indexed) is built ONCE from bind-pose normals (BuildMeshletTriangleAttributes,
// meshlet_vertex_attributes.h). The skinning compute writes the per-frame DEFORMED normals to the skinned-normal
// buffer in ATTRIBUTE-stream space (skinning_cs.slang). This pass re-derives each triangle-corner normal slot from
// the current deformed normals so the RT shadow + caustic photon traces bend on the live pose, not the bind pose.
// It is a GPU mirror of BuildMeshletTriangleAttributes with one substitution: the normal source is the skinned
// (deformed) stream instead of the bind-pose stream. uv0 is pose-invariant, so only the 8-byte normal half is written
// (bytes 8..15 of each record are left verbatim).
[[vk::binding(NWB_SKINNED_MESH_REPACK_BINDING_MESHLET_DESC, NWB_SKINNED_MESH_REPACK_SET)]] StructuredBuffer<NwbMeshletDesc, Std430DataLayout> g_NwbSkinnedMeshRepackMeshlets;
[[vk::binding(NWB_SKINNED_MESH_REPACK_BINDING_PRIMITIVE_INDICES, NWB_SKINNED_MESH_REPACK_SET)]] ByteAddressBuffer g_NwbSkinnedMeshRepackPrimitiveIndices;
[[vk::binding(NWB_SKINNED_MESH_REPACK_BINDING_ATTRIBUTE_REF_DELTAS, NWB_SKINNED_MESH_REPACK_SET)]] ByteAddressBuffer g_NwbSkinnedMeshRepackAttributeRefDeltas;
[[vk::binding(NWB_SKINNED_MESH_REPACK_BINDING_LOCAL_VERTEX_REFS, NWB_SKINNED_MESH_REPACK_SET)]] StructuredBuffer<NwbMeshletLocalVertexRef, Std430DataLayout> g_NwbSkinnedMeshRepackLocalVertexRefs;
[[vk::binding(NWB_SKINNED_MESH_REPACK_BINDING_SKINNED_NORMALS, NWB_SKINNED_MESH_REPACK_SET)]] StructuredBuffer<NwbMeshHalf4, Std430DataLayout> g_NwbSkinnedMeshRepackSkinnedNormals;
[[vk::binding(NWB_SKINNED_MESH_REPACK_BINDING_ATTRIBUTE_BUFFER, NWB_SKINNED_MESH_REPACK_SET)]] RWByteAddressBuffer g_NwbSkinnedMeshRepackAttributeBuffer;

struct NwbSkinnedMeshRepackPushConstants{
    uint4 payload0;
};

[[vk::push_constant]]
ConstantBuffer<NwbSkinnedMeshRepackPushConstants> g_NwbSkinnedMeshRepackPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbSkinnedMeshRepackMeshletCount(){
    return g_NwbSkinnedMeshRepackPushConstants.payload0[NWB_SKINNED_MESH_REPACK_PUSH_MESHLET_COUNT];
}

uint nwbSkinnedMeshRepackLoadPrimitiveIndex(const uint primitiveIndex){
    const uint word = g_NwbSkinnedMeshRepackPrimitiveIndices.Load(primitiveIndex & ~NWB_MESHLET_PACKED_WORD_BYTE_MASK);
    return (word >> ((primitiveIndex & NWB_MESHLET_PACKED_WORD_BYTE_MASK) * NWB_MESHLET_PACKED_BYTE_BITS)) & NWB_MESHLET_PACKED_BYTE_MASK;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One thread group per meshlet; threads stride over the meshlet's primitive-corner refs, exactly like the CPU
// triangle-attribute builder. The write SLOT is the primitive byte offset (primitive*3+corner); the normal VALUE comes
// from the global attribute (normal) index for that local vertex.
[numthreads(NWB_SKINNED_MESH_REPACK_GROUP_SIZE_X, 1, 1)]
void main(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint meshletIndex = groupID.x;
    if(meshletIndex >= nwbSkinnedMeshRepackMeshletCount())
        return;

    const NwbMeshletDesc meshlet = g_NwbSkinnedMeshRepackMeshlets[meshletIndex];
    const NwbMeshletRefDecodeInfo decodeInfo = nwbMeshletBuildRefDecodeInfo(meshlet);
    const uint primitiveCornerCount = nwbMeshletPrimitiveCount(meshlet) * NWB_MESHLET_TRIANGLE_INDEX_COUNT;

    for(uint primitiveCorner = groupThreadID.x; primitiveCorner < primitiveCornerCount; primitiveCorner += NWB_SKINNED_MESH_REPACK_GROUP_SIZE_X){
        const uint primitiveByte = meshlet.primitiveOffset + primitiveCorner;
        const uint localVertex = nwbSkinnedMeshRepackLoadPrimitiveIndex(primitiveByte);
        const NwbMeshletLocalVertexRef localVertexRef = g_NwbSkinnedMeshRepackLocalVertexRefs[meshlet.localVertexOffset + localVertex];
        const uint localAttributeIndex = nwbMeshletLocalAttribute(localVertexRef);
        const NwbMeshletAttributeStreamRef attributeRef = nwbMeshletDecodeAttributeRef(
            g_NwbSkinnedMeshRepackAttributeRefDeltas,
            meshlet,
            decodeInfo,
            localAttributeIndex
        );

        // AttribGpu stride = 16: Half4 normal @ +0 (xy, zw), Float2 uv0 @ +8. Write ONLY the 8-byte normal halves;
        // uv0 (bytes 8..15) is pose-invariant and stays as uploaded.
        const NwbMeshHalf4 skinnedNormal = g_NwbSkinnedMeshRepackSkinnedNormals[attributeRef.normal];
        g_NwbSkinnedMeshRepackAttributeBuffer.Store2(primitiveByte * 16u, uint2(skinnedNormal.xy, skinnedNormal.zw));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

