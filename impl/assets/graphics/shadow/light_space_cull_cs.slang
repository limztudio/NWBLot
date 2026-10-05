// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_scene.slangi"
#include "light_space_cull.slangi"
#include "../mesh/runtime_bounds_constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_LIGHT_SPACE_CULL_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint commandIndex = dispatchThreadID.x
        + dispatchThreadID.z * NWB_LIGHT_SPACE_CULL_MAX_GROUPS_X * NWB_LIGHT_SPACE_CULL_GROUP_SIZE;
    const uint commandCount = g_NwbLightSpacePush.meshletCount / NWB_LIGHT_SPACE_MESHLETS_PER_DRAW
        + (g_NwbLightSpacePush.meshletCount % NWB_LIGHT_SPACE_MESHLETS_PER_DRAW != 0u ? 1u : 0u);
    if(commandIndex >= max(commandCount, 1u) || dispatchThreadID.y >= g_NwbLightSpacePush.viewCount)
        return;
    const NwbLightSpaceView view = nwbLightSpaceLoadView(dispatchThreadID.y);
    uint indexCount = 0u;
    uint firstIndex = 0u;
    bool outside = false;
    if(g_NwbLightSpacePush.meshletCount != 0u && (view.light.w & NWB_LIGHT_SPACE_FLAG_POINT) != 0u){
        const uint firstMeshlet = commandIndex * NWB_LIGHT_SPACE_MESHLETS_PER_DRAW;
        const uint endMeshlet = firstMeshlet
            + min(NWB_LIGHT_SPACE_MESHLETS_PER_DRAW, g_NwbLightSpacePush.meshletCount - firstMeshlet);
        const NwbMeshletDesc first = nwbHeapMeshletDescs(g_NwbLightSpacePush.meshletDescSlot)[firstMeshlet];
        const NwbMeshletDesc last = nwbHeapMeshletDescs(g_NwbLightSpacePush.meshletDescSlot)[endMeshlet - 1u];
        firstIndex = first.primitiveOffset;
        indexCount = last.primitiveOffset + nwbMeshletPrimitiveCount(last) * NWB_MESHLET_TRIANGLE_INDEX_COUNT - firstIndex;
        const ByteAddressBuffer bounds = nwbHeapRawBuffer(g_NwbLightSpacePush.meshletBoundsSlot);
        float3 minimum = float3(asfloat(0x7f7fffffu));
        float3 maximum = -minimum;
        bool boundsValid = true;
        [loop]
        for(uint meshletIndex = firstMeshlet; meshletIndex < endMeshlet; ++meshletIndex){
            const uint boundsAddress = meshletIndex * NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE;
            const uint flags = bounds.Load(boundsAddress + NWB_RUNTIME_MESH_BOUNDS_FLAGS_OFFSET);
            if((flags & NWB_RUNTIME_MESH_BOUNDS_FLAG_VALID) == 0u){
                boundsValid = false;
                break;
            }
            const float3 meshletMin = asfloat(bounds.Load3(boundsAddress + NWB_RUNTIME_MESH_BOUNDS_MIN_OFFSET));
            const float3 meshletMax = asfloat(bounds.Load3(boundsAddress + NWB_RUNTIME_MESH_BOUNDS_MAX_OFFSET));
            const uint3 minimumBits = asuint(meshletMin) & 0x7fffffffu;
            const uint3 maximumBits = asuint(meshletMax) & 0x7fffffffu;
            if(
                !all(isfinite(meshletMin)) || !all(isfinite(meshletMax)) || any(meshletMin > meshletMax)
                || any((minimumBits != 0u) & (minimumBits < 0x00800000u))
                || any((maximumBits != 0u) & (maximumBits < 0x00800000u))
            ){
                boundsValid = false;
                break;
            }
            minimum = min(minimum, meshletMin);
            maximum = max(maximum, meshletMax);
        }
        if(boundsValid)
            outside = nwbLightSpaceBoundsOutside(view, g_NwbLightSpacePush.instanceIndex, minimum, maximum);
    }
    else if(commandIndex == 0u){
        const NwbSwBvhInstance instance = nwbLightSpaceInstance(g_NwbLightSpacePush.instanceIndex);
        const NwbBvhNode root = nwbLightSpaceMeshRoot(g_NwbLightSpacePush.instanceIndex);
        indexCount = instance.primitiveCount * NWB_MESHLET_TRIANGLE_INDEX_COUNT;
        outside = nwbLightSpaceBoundsOutside(view, g_NwbLightSpacePush.instanceIndex, root.aabbMin, root.aabbMax);
    }
    const uint drawIndex = dispatchThreadID.y * g_NwbLightSpacePush.captureDrawCount
        + g_NwbLightSpacePush.casterDrawOffset + commandIndex;
    const uint address = drawIndex * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES;
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.outputSlot].Store4(address, uint4(outside ? 0u : indexCount, 1u, firstIndex, 0u));
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.outputSlot].Store(address + 16u, firstIndex / NWB_MESHLET_TRIANGLE_INDEX_COUNT);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

