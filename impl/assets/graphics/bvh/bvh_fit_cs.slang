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
globallycoherent RWStructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbBvhNodes;

// Quantized (20B NwbBvhNodeQ) mirror of the float tree the traversal reads. The fit writes both: the float tree
// (g_NwbBvhNodes) for its own sibling-read unions / refit / self-test, and this quantized mirror for the traversal's
// reduced-bandwidth reads. The reference box is the build push-constants aabbMin/aabbMax (the mesh OBJECT-space bounds),
// matching the CPU-published per-mesh ref-bounds element [1 + meshIndex].
[[vk::binding(NWB_BVH_BUILD_BINDING_QNODES, NWB_BVH_BUILD_SET)]]
RWStructuredBuffer<NwbBvhNodeQ, Std430DataLayout> g_NwbBvhQNodes;

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

    // A full build reads the triangle from the freshly sorted payload and stores it in the leaf; a refit
    // keeps the build-pose topology and reuses the primitive the leaf already holds, so it only recomputes
    // boxes from the new (e.g. skinned) positions and leaves the child links untouched.
    const bool refit = g_NwbBvhBuildPushConstants.refitMode != 0u;
    const uint triangle = refit
        ? (g_NwbBvhNodes[leafNodeIndex].leftChild & ~NWB_BVH_LEAF_FLAG)
        : g_NwbBvhPayload[leaf];

    // The mesh OBJECT-space reference box (build push constants) is the same box the CPU publishes to the
    // traversal's per-mesh ref-bounds element [1 + meshIndex], so the quantized mirror dequantizes to a superset
    // of the float tree. invExtent is per-axis 1/max(extent,1e-8).
    const float3 refMin = g_NwbBvhBuildPushConstants.aabbMin.xyz;
    const float3 refExtent = max(g_NwbBvhBuildPushConstants.aabbMax.xyz - refMin, float3(1e-8, 1e-8, 1e-8));
    const float3 invExtent = float3(1.0, 1.0, 1.0) / refExtent;
    const uint indexBase = triangle * 3u;
    const uint i0 = g_NwbBvhTriangleIndices.Load(indexBase * 4u);
    const uint i1 = g_NwbBvhTriangleIndices.Load((indexBase + 1u) * 4u);
    const uint i2 = g_NwbBvhTriangleIndices.Load((indexBase + 2u) * 4u);
    const float3 p0 = asfloat(g_NwbBvhPositions.Load3(i0 * 12u));
    const float3 p1 = asfloat(g_NwbBvhPositions.Load3(i1 * 12u));
    const float3 p2 = asfloat(g_NwbBvhPositions.Load3(i2 * 12u));

    g_NwbBvhNodes[leafNodeIndex].aabbMin = min(p0, min(p1, p2));
    g_NwbBvhNodes[leafNodeIndex].aabbMax = max(p0, max(p1, p2));
    if(!refit){
        g_NwbBvhNodes[leafNodeIndex].leftChild = NWB_BVH_LEAF_FLAG | triangle;
        g_NwbBvhNodes[leafNodeIndex].rightChild = 1u;
    }
    // Quantized mirror of this leaf node (same child links; packed quantized box against the mesh reference).
    {
        const float3 boxMin = min(p0, min(p1, p2));
        const float3 boxMax = max(p0, max(p1, p2));
        uint qX, qY, qZ;
        nwbBvhQuantizeBounds(boxMin, boxMax, refMin, invExtent, qX, qY, qZ);
        g_NwbBvhQNodes[leafNodeIndex].qX = qX;
        g_NwbBvhQNodes[leafNodeIndex].qY = qY;
        g_NwbBvhQNodes[leafNodeIndex].qZ = qZ;
        g_NwbBvhQNodes[leafNodeIndex].leftChild = g_NwbBvhNodes[leafNodeIndex].leftChild;
        g_NwbBvhQNodes[leafNodeIndex].rightChild = g_NwbBvhNodes[leafNodeIndex].rightChild;
    }

    DeviceMemoryBarrier();

    uint nodeIndex = g_NwbBvhParent[leafNodeIndex];
    while(nodeIndex != NWB_BVH_INVALID){
        uint previousVisits;
        InterlockedAdd(g_NwbBvhVisitCounter[nodeIndex], 1u, previousVisits);
        if(previousVisits == 0u)
            break;

        const uint leftChild = g_NwbBvhNodes[nodeIndex].leftChild;
        const uint rightChild = g_NwbBvhNodes[nodeIndex].rightChild;
        const float3 childMin = min(g_NwbBvhNodes[leftChild].aabbMin, g_NwbBvhNodes[rightChild].aabbMin);
        const float3 childMax = max(g_NwbBvhNodes[leftChild].aabbMax, g_NwbBvhNodes[rightChild].aabbMax);
        g_NwbBvhNodes[nodeIndex].aabbMin = childMin;
        g_NwbBvhNodes[nodeIndex].aabbMax = childMax;
        // Quantized mirror of this internal node (same child links; packed unioned box against the mesh reference).
        uint qX, qY, qZ;
        nwbBvhQuantizeBounds(childMin, childMax, refMin, invExtent, qX, qY, qZ);
        g_NwbBvhQNodes[nodeIndex].qX = qX;
        g_NwbBvhQNodes[nodeIndex].qY = qY;
        g_NwbBvhQNodes[nodeIndex].qZ = qZ;
        g_NwbBvhQNodes[nodeIndex].leftChild = leftChild;
        g_NwbBvhQNodes[nodeIndex].rightChild = rightChild;

        DeviceMemoryBarrier();

        if(nodeIndex == 0u)
            break;
        nodeIndex = g_NwbBvhParent[nodeIndex];
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

