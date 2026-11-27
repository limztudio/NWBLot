// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scene_refit_constants.h"
#include "bvh_heap_resources.slangi"
#include "bindless_heap_bvh.slangi"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbSceneBvhRefitPushConstants{
    uint nodeCount;
    uint sceneNodeSlot;
    uint instanceInputSlot;
    uint instanceCount;
    uint parentSlot;
    uint visitCounterSlot;
    uint passMode;
};
[[vk::push_constant]] ConstantBuffer<NwbSceneBvhRefitPushConstants> g_NwbSceneBvhRefitPush;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const float NWB_SCENE_BVH_REFIT_ROUNDOFF_EPSILON_SCALE = 16.0;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbSceneRefitAxis(const float4 row, const float3 localMin, const float3 localMax, out float minimum, out float maximum){
    const float3 first = row.xyz * localMin;
    const float3 second = row.xyz * localMax;
    const float3 low = min(first, second);
    const float3 high = max(first, second);
    const float3 terms = max(abs(first), abs(second));
    const float magnitude = abs(row.w) + terms.x + terms.y + terms.z + 1.0;
    const float3 inputFlush = abs(row.xyz) * NWB_FLOAT_MIN_NORMAL;
    const float relativeError = NWB_SCENE_BVH_REFIT_ROUNDOFF_EPSILON_SCALE * NWB_FLOAT_EPSILON;
    const float flushError = (inputFlush.x + inputFlush.y + inputFlush.z) * (1.0 + relativeError);
    const float margin = magnitude * relativeError + flushError;
    minimum = ((row.w + low.x) + low.y) + low.z - margin;
    maximum = ((row.w + high.x) + high.y) + high.z + margin;
    return all(isfinite(row)) && all(isfinite(first)) && all(isfinite(second)) && isfinite(minimum) && isfinite(maximum) && minimum <= maximum;
}

[shader("compute")]
[numthreads(NWB_SCENE_BVH_REFIT_GROUP_SIZE, 1, 1)]
void main(const uint3 dispatchThreadId : SV_DispatchThreadID){
    const uint nodeCount = g_NwbSceneBvhRefitPush.nodeCount;
    const uint index = dispatchThreadId.x;
    if(index >= nodeCount)
        return;

    const uint sceneSlot = g_NwbSceneBvhRefitPush.sceneNodeSlot;
    const uint parentSlot = g_NwbSceneBvhRefitPush.parentSlot;
    const uint visitCounterSlot = g_NwbSceneBvhRefitPush.visitCounterSlot;
    if(g_NwbSceneBvhRefitPush.passMode == NWB_SCENE_BVH_REFIT_MODE_INIT_PARENTS){
        if(index == 0u)
            g_NwbHeapBvhWritableUintBuffers[parentSlot][0u] = NWB_BVH_INVALID;
        g_NwbHeapBvhWritableUintBuffers[visitCounterSlot][index] = 0u;
        const NwbBvhNode node = g_NwbHeapBvhWritableNodeBuffers[sceneSlot][index];
        if(!nwbBvhNodeIsLeaf(node)){
            g_NwbHeapBvhWritableUintBuffers[parentSlot][node.leftChild] = index;
            g_NwbHeapBvhWritableUintBuffers[parentSlot][nwbBvhNodeRightChildIndex(node)] = index;
        }
        return;
    }

    NwbBvhNode node = g_NwbHeapBvhWritableNodeBuffers[sceneSlot][index];
    if(!nwbBvhNodeIsLeaf(node))
        return;

    const float finiteMax = asfloat(NWB_FLOAT_MAX_BITS);
    float3 worldMin = -finiteMax;
    float3 worldMax = finiteMax;
    const uint instance = nwbBvhLeafPrimitive(node);
    if(instance < g_NwbSceneBvhRefitPush.instanceCount){
        const uint offset = instance * NWB_SCENE_BVH_REFIT_INSTANCE_BYTES;
        const ByteAddressBuffer inputs = nwbHeapRawBuffer(g_NwbSceneBvhRefitPush.instanceInputSlot);
        const uint meshSlot = inputs.Load(offset + NWB_SCENE_BVH_REFIT_MESH_SLOT_OFFSET);
        const NwbBvhNode root = nwbHeapBvhNodes(meshSlot)[0u];
        float3 lower;
        float3 upper;
        bool valid = all(isfinite(root.aabbMin)) && all(isfinite(root.aabbMax)) && all(root.aabbMin <= root.aabbMax);
        [unroll] for(uint axis = 0u; axis < 3u; ++axis){
            const float4 row = asfloat(inputs.Load4(offset + axis * NWB_SCENE_BVH_REFIT_ROW_BYTES));
            valid = nwbSceneRefitAxis(row, root.aabbMin, root.aabbMax, lower[axis], upper[axis]) && valid;
        }
        if(valid){
            worldMin = lower;
            worldMax = upper;
        }
    }
    node.aabbMin = worldMin;
    node.aabbMax = worldMax;
    g_NwbHeapBvhWritableNodeBuffers[sceneSlot][index] = node;

    DeviceMemoryBarrier();

    uint current = g_NwbHeapBvhWritableUintBuffers[parentSlot][index];
    while(current != NWB_BVH_INVALID){
        uint previousVisits;
        InterlockedAdd(g_NwbHeapBvhWritableUintBuffers[visitCounterSlot][current], 1u, previousVisits);
        if(previousVisits == 0u)
            break;
        g_NwbHeapBvhWritableUintBuffers[visitCounterSlot][current] = 0u;

        NwbBvhNode internalNode = g_NwbHeapBvhWritableNodeBuffers[sceneSlot][current];
        const NwbBvhNode left = g_NwbHeapBvhWritableNodeBuffers[sceneSlot][internalNode.leftChild];
        const NwbBvhNode right = g_NwbHeapBvhWritableNodeBuffers[sceneSlot][nwbBvhNodeRightChildIndex(internalNode)];
        internalNode.aabbMin = min(left.aabbMin, right.aabbMin);
        internalNode.aabbMax = max(left.aabbMax, right.aabbMax);
        g_NwbHeapBvhWritableNodeBuffers[sceneSlot][current] = internalNode;

        DeviceMemoryBarrier();

        if(current == 0u)
            break;
        current = g_NwbHeapBvhWritableUintBuffers[parentSlot][current];
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

