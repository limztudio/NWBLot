
#include "binding_slots.h"
#include "bvh_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::push_constant]]
ConstantBuffer<NwbBvhBuildPushConstants> g_NwbBvhBuildPushConstants;

[[vk::binding(NWB_BVH_BUILD_BINDING_KEYS, NWB_BVH_BUILD_SET)]]
RWStructuredBuffer<uint> g_NwbBvhKeys;

[[vk::binding(NWB_BVH_BUILD_BINDING_NODES, NWB_BVH_BUILD_SET)]]
RWStructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbBvhNodes;

[[vk::binding(NWB_BVH_BUILD_BINDING_PARENT, NWB_BVH_BUILD_SET)]]
RWStructuredBuffer<uint> g_NwbBvhParent;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Common-prefix length of the augmented keys: 32-bit Morton, then the leaf index as a tie-break so equal
// Morton codes still yield a strict total order (Karras requires this to avoid degenerate nodes). Returns
// -1 for an out-of-range neighbour, which terminates the range searches below.
int nwbBvhDelta(const int lhs, const int rhs, const uint primitiveCount){
    if(rhs < 0 || rhs >= (int)primitiveCount)
        return -1;
    const uint keyLhs = g_NwbBvhKeys[lhs];
    const uint keyRhs = g_NwbBvhKeys[rhs];
    if(keyLhs != keyRhs)
        return (int)nwbBvhClz(keyLhs ^ keyRhs);
    return 32 + (int)nwbBvhClz((uint)lhs ^ (uint)rhs);
}

// Karras split: the last key index of the left child's range within [first, last].
int nwbBvhFindSplit(const int first, const int last, const uint primitiveCount){
    const int commonPrefix = nwbBvhDelta(first, last, primitiveCount);
    int split = first;
    int step = last - first;
    do{
        step = (step + 1) >> 1;
        const int candidate = split + step;
        if(candidate < last && nwbBvhDelta(first, candidate, primitiveCount) > commonPrefix)
            split = candidate;
    }while(step > 1);
    return split;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One thread per internal node: determine its key range (direction + binary-searched far end), split it,
// and link the two children, recording each child's parent for the bottom-up fit. The host clears the
// parent buffer to NWB_BVH_INVALID first, so the root (internal node 0, never a child) keeps no parent.
[numthreads(NWB_BVH_BUILD_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint internalIndex = dispatchThreadID.x;
    const uint primitiveCount = g_NwbBvhBuildPushConstants.primitiveCount;
    if(internalIndex >= g_NwbBvhBuildPushConstants.internalCount)
        return;

    const int node = (int)internalIndex;

    const int direction = (nwbBvhDelta(node, node + 1, primitiveCount) - nwbBvhDelta(node, node - 1, primitiveCount)) >= 0 ? 1 : -1;
    const int deltaMin = nwbBvhDelta(node, node - direction, primitiveCount);

    int lengthMax = 2;
    while(nwbBvhDelta(node, node + lengthMax * direction, primitiveCount) > deltaMin)
        lengthMax <<= 1;

    int length = 0;
    for(int step = lengthMax >> 1; step >= 1; step >>= 1){
        if(nwbBvhDelta(node, node + (length + step) * direction, primitiveCount) > deltaMin)
            length += step;
    }
    const int other = node + length * direction;

    const int first = min(node, other);
    const int last = max(node, other);
    const int split = nwbBvhFindSplit(first, last, primitiveCount);

    const uint internalCount = primitiveCount - 1u;
    const uint leftChild = (split == first) ? (internalCount + (uint)split) : (uint)split;
    const uint rightChild = ((split + 1) == last) ? (internalCount + (uint)(split + 1)) : (uint)(split + 1);

    g_NwbBvhNodes[internalIndex].leftChild = leftChild;
    g_NwbBvhNodes[internalIndex].rightChild = rightChild;
    g_NwbBvhParent[leftChild] = internalIndex;
    g_NwbBvhParent[rightChild] = internalIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

