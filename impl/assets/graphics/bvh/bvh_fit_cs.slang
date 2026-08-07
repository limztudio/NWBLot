// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bvh_triangle_input.slangi"
#include "bvh_heap_resources.slangi"


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

    // A full build reads the triangle from the freshly sorted payload and stores it in the leaf; a refit
    // keeps the build-pose topology and reuses the primitive the leaf already holds, so it only recomputes
    // boxes from the new (e.g. skinned) positions and leaves the child links untouched.
    const bool refit = g_NwbBvhBuildPushConstants.refitMode != NWB_BVH_BUILD_MODE_FULL;
    const uint triangle = refit
        ? (g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][leafNodeIndex].leftChild & ~NWB_BVH_LEAF_FLAG)
        : g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBuildPushConstants.payloadHeapSlot][leaf];
    float3 p0;
    float3 p1;
    float3 p2;
    nwbBvhLoadTrianglePositions(triangle, p0, p1, p2);

    g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][leafNodeIndex].aabbMin = min(p0, min(p1, p2));
    g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][leafNodeIndex].aabbMax = max(p0, max(p1, p2));
    if(!refit){
        g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][leafNodeIndex].leftChild = NWB_BVH_LEAF_FLAG | triangle;
        g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][leafNodeIndex].rightChild = 1u;
    }

    DeviceMemoryBarrier();

    uint nodeIndex = g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBuildPushConstants.parentHeapSlot][leafNodeIndex];
    while(nodeIndex != NWB_BVH_INVALID){
        uint previousVisits;
        InterlockedAdd(g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBuildPushConstants.visitCounterHeapSlot][nodeIndex], 1u, previousVisits);
        if(previousVisits == 0u)
            break;

        const uint leftChild = g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][nodeIndex].leftChild;
        const uint rightChild = g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][nodeIndex].rightChild;
        g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][nodeIndex].aabbMin = min(
            g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][leftChild].aabbMin,
            g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][rightChild].aabbMin
        );
        g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][nodeIndex].aabbMax = max(
            g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][leftChild].aabbMax,
            g_NwbHeapBvhWritableNodeBuffers[g_NwbBvhBuildPushConstants.nodeHeapSlot][rightChild].aabbMax
        );

        DeviceMemoryBarrier();

        if(nodeIndex == 0u)
            break;
        nodeIndex = g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBuildPushConstants.parentHeapSlot][nodeIndex];
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

