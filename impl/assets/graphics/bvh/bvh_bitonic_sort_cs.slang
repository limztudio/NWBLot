////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Bitonic sort, two regimes sharing one shader + push-constant `mode`:
//
//   mode == LOCAL_TILE : one dispatch sorts every GROUP_SIZE-element tile in place. Each workgroup owns a
//                        contiguous tile; it loads the tile into groupshared, runs the full intra-tile bitonic
//                        network (sequenceSize 2..GROUP_SIZE) with `compareDistance` halving each round, then
//                        writes the sorted tile back. This collapses the `log2(GROUP_SIZE)*(log2(GROUP_SIZE)+1)/2`
//                        compare-swap sub-steps that the GLOBAL regime would otherwise issue as one dispatch each.
//
//   mode == GLOBAL     : the merge step for `sequenceSize > GROUP_SIZE`. Each thread owns ONE element and the
//                        XOR partner `(index ^ compareDistance)` always lands in a DIFFERENT tile, so the swap is
//                        a plain global-memory read/conditional-write — the original kernel, unchanged in math.
//
// Order invariant: both regimes use the same `nwbBvhSortPairLess` (lexicographic (key,payload)) and the same
// `(index & sequenceSize)` ascending test, so they compose into the standard bitonic sort. Sorting each tile
// first just turns every GROUP_SIZE run into a sorted (hence bitonic) sequence, which is exactly what the
// GLOBAL merge steps at sequenceSize <= GROUP_SIZE would have produced; the larger merges are unaffected.


struct NwbBvhBitonicSortPushConstants{
    uint elementCount;       // real element count (tail beyond it holds the 0xFFFFFFFF sentinel padding)
    uint compareDistance;    // GLOBAL only: XOR distance for this merge step
    uint sequenceSize;       // GLOBAL only: current bitonic sequence size (a power of two > GROUP_SIZE)
    uint mode;               // 0 = LOCAL_TILE, 1 = GLOBAL
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


groupshared uint s_tileKeys[NWB_BVH_SORT_GROUP_SIZE];
groupshared uint s_tilePayload[NWB_BVH_SORT_GROUP_SIZE];


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One dispatch per tile: load -> full intra-tile bitonic sort in groupshared -> store. The group's tile is
// [tileBase .. tileBase+GROUP_SIZE); the caller guarantees paddedCount is a multiple of GROUP_SIZE, so every
// thread maps 1:1 to a tile element (the sentinel-padded tail sorts to the end naturally).
[numthreads(NWB_BVH_SORT_GROUP_SIZE, 1, 1)]
void nwbBvhSortLocalTile(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID){
    const uint tileBase = groupID.x * NWB_BVH_SORT_GROUP_SIZE;
    const uint local = groupThreadID.x;
    const uint global = tileBase + local;

    s_tileKeys[local] = g_NwbBvhSortKeys[global];
    s_tilePayload[local] = g_NwbBvhSortPayload[global];
    GroupMemoryBarrierWithGroupSync();

    // Standard bitonic network over the GROUP_SIZE tile: build a sorted (bitonic) sequence by growing
    // sequenceSize 2->GROUP_SIZE, halving compareDistance each inner round. Same compare-swap math as the
    // GLOBAL regime, just over shared memory with all threads cooperating.
    [unroll]
    for(uint sequenceSize = 2u; sequenceSize <= NWB_BVH_SORT_GROUP_SIZE; sequenceSize <<= 1u){
        [unroll]
        for(uint compareDistance = sequenceSize >> 1u; compareDistance > 0u; compareDistance >>= 1u){
            const uint partner = local ^ compareDistance;
            if(partner > local){
                const bool ascending = (local & sequenceSize) == 0u;
                const uint keyA = s_tileKeys[local];
                const uint payloadA = s_tilePayload[local];
                const uint keyB = s_tileKeys[partner];
                const uint payloadB = s_tilePayload[partner];
                const bool aLessB = nwbBvhSortPairLess(keyA, payloadA, keyB, payloadB);
                if(ascending != aLessB){
                    s_tileKeys[local] = keyB;
                    s_tilePayload[local] = payloadB;
                    s_tileKeys[partner] = keyA;
                    s_tilePayload[partner] = payloadA;
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }

    g_NwbBvhSortKeys[global] = s_tileKeys[local];
    g_NwbBvhSortPayload[global] = s_tilePayload[local];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One dispatch per merge step for sequenceSize > GROUP_SIZE. Each thread owns element `index`; its XOR partner
// is in a different tile, so this is a global-memory compare-swap — the original kernel's math, unchanged.
[numthreads(NWB_BVH_SORT_GROUP_SIZE, 1, 1)]
void nwbBvhSortGlobalMerge(uint3 dispatchThreadID : SV_DispatchThreadID){
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


[numthreads(NWB_BVH_SORT_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID){
    if(g_NwbBvhBitonicSortPushConstants.mode == 0u)
        nwbBvhSortLocalTile(groupID, groupThreadID);
    else
        nwbBvhSortGlobalMerge(dispatchThreadID);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
