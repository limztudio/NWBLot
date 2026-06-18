// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbBvhBitonicSortPushConstants{
    uint elementCount;
    uint compareDistance;
    uint sequenceSize;
    uint pad0;
};

[[vk::push_constant]]
ConstantBuffer<NwbBvhBitonicSortPushConstants> g_NwbBvhBitonicSortPushConstants;

[[vk::binding(NWB_BVH_SORT_BINDING_KEYS, NWB_BVH_SORT_SET)]]
RWStructuredBuffer<uint> g_NwbBvhSortKeys;

[[vk::binding(NWB_BVH_SORT_BINDING_PAYLOAD, NWB_BVH_SORT_SET)]]
RWStructuredBuffer<uint> g_NwbBvhSortPayload;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Lexicographic (key, payload) order: equal Morton codes fall back to the primitive index so the comparison
// is a strict total order — Karras topology needs unique sort keys to avoid degenerate internal nodes.
bool nwbBvhSortPairLess(const uint keyA, const uint payloadA, const uint keyB, const uint payloadB){
    if(keyA != keyB)
        return keyA < keyB;
    return payloadA < payloadB;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_BVH_SORT_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint index = dispatchThreadID.x;
    const uint partner = index ^ g_NwbBvhBitonicSortPushConstants.compareDistance;
    if(partner <= index)
        return;

    const uint keyA = g_NwbBvhSortKeys[index];
    const uint payloadA = g_NwbBvhSortPayload[index];
    const uint keyB = g_NwbBvhSortKeys[partner];
    const uint payloadB = g_NwbBvhSortPayload[partner];

    const bool ascending = (index & g_NwbBvhBitonicSortPushConstants.sequenceSize) == 0u;
    const bool aLessB = nwbBvhSortPairLess(keyA, payloadA, keyB, payloadB);
    if(ascending != aLessB){
        g_NwbBvhSortKeys[index] = keyB;
        g_NwbBvhSortPayload[index] = payloadB;
        g_NwbBvhSortKeys[partner] = keyA;
        g_NwbBvhSortPayload[partner] = payloadA;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

