// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../bvh/constants.h"
#include "bindless_resources.slangi"
#include "constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbSkinnedMeshRepackPushConstants{
    uint2 payload0;
};

[[vk::push_constant]] ConstantBuffer<NwbSkinnedMeshRepackPushConstants> g_NwbSkinnedMeshRepackPushConstants;

#define g_NwbSkinnedMeshRepackBindlessResources nwbSkinnedMeshHeapBindlessResources(g_NwbSkinnedMeshRepackPushConstants.payload0[NWB_SKINNED_MESH_REPACK_PUSH_BINDLESS_RESOURCES_SLOT])


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbSkinnedMeshRepackMeshletCount(){
    return g_NwbSkinnedMeshRepackPushConstants.payload0[NWB_SKINNED_MESH_REPACK_PUSH_MESHLET_COUNT];
}

uint nwbSkinnedMeshRepackLoadPrimitiveIndex(const uint primitiveIndex){
    const uint word = nwbHeapRawBuffer(g_NwbSkinnedMeshRepackBindlessResources.streamSlots3.y).Load(primitiveIndex & ~NWB_MESHLET_PACKED_WORD_BYTE_MASK);
    return (word >> ((primitiveIndex & NWB_MESHLET_PACKED_WORD_BYTE_MASK) * NWB_MESHLET_PACKED_BYTE_BITS)) & NWB_MESHLET_PACKED_BYTE_MASK;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SKINNED_MESH_REPACK_GROUP_SIZE_X, 1, 1)]
void main(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint meshletIndex = groupID.x;
    if(meshletIndex >= nwbSkinnedMeshRepackMeshletCount())
        return;

    const NwbMeshletDesc meshlet = nwbSkinnedMeshHeapMeshlets(g_NwbSkinnedMeshRepackBindlessResources.streamSlots1.z)[meshletIndex];
    const NwbMeshletRefDecodeInfo decodeInfo = nwbMeshletBuildRefDecodeInfo(meshlet);
    const uint primitiveCornerCount = nwbMeshletPrimitiveCount(meshlet) * NWB_MESHLET_TRIANGLE_INDEX_COUNT;

    for(uint primitiveCorner = groupThreadID.x; primitiveCorner < primitiveCornerCount; primitiveCorner += NWB_SKINNED_MESH_REPACK_GROUP_SIZE_X){
        const uint primitiveByte = meshlet.primitiveOffset + primitiveCorner;
        const uint localVertex = nwbSkinnedMeshRepackLoadPrimitiveIndex(primitiveByte);
        const NwbMeshletLocalVertexRef localVertexRef = nwbSkinnedMeshHeapLocalVertexRefs(g_NwbSkinnedMeshRepackBindlessResources.streamSlots3.x)[meshlet.localVertexOffset + localVertex];
        const uint localAttributeIndex = nwbMeshletLocalAttribute(localVertexRef);
        const NwbMeshletAttributeStreamRef attributeRef = nwbMeshletDecodeAttributeRef(nwbHeapRawBuffer(g_NwbSkinnedMeshRepackBindlessResources.streamSlots2.x), meshlet, decodeInfo, localAttributeIndex);

        const NwbMeshHalf4 skinnedNormal = nwbSkinnedMeshHeapHalf4s(g_NwbSkinnedMeshRepackBindlessResources.streamSlots0.w)[attributeRef.normal];
        nwbSkinnedMeshHeapWritableRawBuffer(g_NwbSkinnedMeshRepackBindlessResources.streamSlots3.w).Store2(primitiveByte * NWB_RAYTRACE_VERTEX_ATTRIBUTE_STRIDE_BYTES, uint2(skinnedNormal.xy, skinnedNormal.zw));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef g_NwbSkinnedMeshRepackBindlessResources


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

