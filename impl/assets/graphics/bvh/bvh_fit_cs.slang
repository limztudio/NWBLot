// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "bvh_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::push_constant]]
ConstantBuffer<NwbBvhBuildPushConstants> g_NwbBvhBuildPushConstants;

[[vk::binding(NWB_BVH_BUILD_BINDING_POSITIONS, NWB_BVH_BUILD_SET)]]
ByteAddressBuffer g_NwbBvhPositions;

[[vk::binding(NWB_BVH_BUILD_BINDING_TRIANGLE_INDICES, NWB_BVH_BUILD_SET)]]
ByteAddressBuffer g_NwbBvhTriangleIndices;

[[vk::binding(NWB_BVH_BUILD_BINDING_PAYLOAD, NWB_BVH_BUILD_SET)]]
RWStructuredBuffer<uint> g_NwbBvhPayload;

[[vk::binding(NWB_BVH_BUILD_BINDING_NODES, NWB_BVH_BUILD_SET)]]
globallycoherent RWStructuredBuffer<NwbBvhNode> g_NwbBvhNodes;

[[vk::binding(NWB_BVH_BUILD_BINDING_PARENT, NWB_BVH_BUILD_SET)]]
RWStructuredBuffer<uint> g_NwbBvhParent;

[[vk::binding(NWB_BVH_BUILD_BINDING_VISIT_COUNTER, NWB_BVH_BUILD_SET)]]
RWStructuredBuffer<uint> g_NwbBvhVisitCounter;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One thread per leaf: write the leaf's triangle box, then walk to the root. An atomic visit counter (host
// cleared to 0) lets only the SECOND child to reach an internal node proceed, so both child boxes are ready
// when it unions them. globallycoherent + DeviceMemoryBarrier publish each box before the counter signals
// it, so the sibling sees it. The host clears the parent buffer to NWB_BVH_INVALID, so a leaf with no parent
// (single-primitive mesh, the root) simply stops.
[numthreads(NWB_BVH_BUILD_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint leaf = dispatchThreadID.x;
    const uint primitiveCount = g_NwbBvhBuildPushConstants.primitiveCount;
    if(leaf >= primitiveCount)
        return;

    const uint internalCount = g_NwbBvhBuildPushConstants.internalCount;
    const uint leafNodeIndex = internalCount + leaf;

    const uint triangle = g_NwbBvhPayload[leaf];
    const uint indexBase = triangle * 3u;
    const uint i0 = g_NwbBvhTriangleIndices.Load(indexBase * 4u);
    const uint i1 = g_NwbBvhTriangleIndices.Load((indexBase + 1u) * 4u);
    const uint i2 = g_NwbBvhTriangleIndices.Load((indexBase + 2u) * 4u);
    const float3 p0 = asfloat(g_NwbBvhPositions.Load3(i0 * 12u));
    const float3 p1 = asfloat(g_NwbBvhPositions.Load3(i1 * 12u));
    const float3 p2 = asfloat(g_NwbBvhPositions.Load3(i2 * 12u));

    NwbBvhNode leafNode;
    leafNode.aabbMin = min(p0, min(p1, p2));
    leafNode.aabbMax = max(p0, max(p1, p2));
    leafNode.leftChild = NWB_BVH_LEAF_FLAG | triangle;
    leafNode.rightChild = 1u;
    g_NwbBvhNodes[leafNodeIndex] = leafNode;

    DeviceMemoryBarrier();

    uint nodeIndex = g_NwbBvhParent[leafNodeIndex];
    while(nodeIndex != NWB_BVH_INVALID){
        uint previousVisits;
        InterlockedAdd(g_NwbBvhVisitCounter[nodeIndex], 1u, previousVisits);
        if(previousVisits == 0u)
            break;

        const uint leftChild = g_NwbBvhNodes[nodeIndex].leftChild;
        const uint rightChild = g_NwbBvhNodes[nodeIndex].rightChild;
        g_NwbBvhNodes[nodeIndex].aabbMin = min(g_NwbBvhNodes[leftChild].aabbMin, g_NwbBvhNodes[rightChild].aabbMin);
        g_NwbBvhNodes[nodeIndex].aabbMax = max(g_NwbBvhNodes[leftChild].aabbMax, g_NwbBvhNodes[rightChild].aabbMax);

        DeviceMemoryBarrier();

        if(nodeIndex == 0u)
            break;
        nodeIndex = g_NwbBvhParent[nodeIndex];
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

