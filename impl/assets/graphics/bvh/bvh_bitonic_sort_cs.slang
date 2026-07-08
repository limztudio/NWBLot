////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Bitonic sort, three regimes sharing one shader + push-constant `mode`:
//
//   mode == LOCAL_TILE  : one dispatch sorts every GROUP_SIZE-element tile in place. Each workgroup owns a
//                         contiguous tile; it loads the tile into groupshared, runs the full intra-tile bitonic
//                         network (sequenceSize 2..GROUP_SIZE) with `compareDistance` halving each round, then
//                         writes the sorted tile back. This collapses the `log2(GROUP_SIZE)*(log2(GROUP_SIZE)+1)/2`
//                         compare-swap sub-steps that issuing one GLOBAL dispatch each would otherwise require.
//
//   mode == GLOBAL      : ONE inter-tile merge step for `sequenceSize > GROUP_SIZE` with `compareDistance >=
//                         GROUP_SIZE`. Each thread owns ONE element and the XOR partner `(index ^ compareDistance)`
//                         lands in a DIFFERENT tile, so the swap is a plain global-memory read/conditional-write.
//
//   mode == GLOBAL_TAIL : the per-sequenceSize INTRA-tile tail for `sequenceSize > GROUP_SIZE`. After the
//                         GLOBAL inter-tile steps (compareDistance >= GROUP_SIZE) are done, the remaining
//                         `compareDistance = GROUP_SIZE/2..1` steps all have their XOR partner inside the SAME
//                         tile, so this one dispatch loads the tile into groupshared and runs them all there —
//                         collapsing 8 dispatches per sequenceSize into one, mirroring the LOCAL_TILE collapse.
//
// Order invariant: all three regimes use the same `nwbBvhSortPairLess` (lexicographic (key,payload)) and the same
// `(index & sequenceSize)` ascending test, so they compose into the standard bitonic sort. Sorting each tile
// first just turns every GROUP_SIZE run into a sorted (hence bitonic) sequence, which is exactly what the
// merge steps at sequenceSize <= GROUP_SIZE would have produced; the larger merges are unaffected.


struct NwbBvhBitonicSortPushConstants{
    uint elementCount;       // real element count (tail beyond it holds the 0xFFFFFFFF sentinel padding)
    uint compareDistance;    // GLOBAL only: XOR distance for this merge step
    uint sequenceSize;       // GLOBAL only: current bitonic sequence size (a power of two > GROUP_SIZE)
    uint mode;               // 0 = LOCAL_TILE, 1 = GLOBAL, 2 = GLOBAL_TAIL
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


// One dispatch per INTER-tile merge step: sequenceSize > GROUP_SIZE with compareDistance >= GROUP_SIZE. Each
// thread owns element `index`; its XOR partner is in a different tile (compareDistance spans the tile stride),
// so this is a global-memory compare-swap — the original kernel's math, unchanged.
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


// One dispatch per sequenceSize for the INTRA-tile tail. After the GLOBAL inter-tile steps complete, the
// remaining compareDistance = GROUP_SIZE/2..1 steps have partners inside the same GROUP_SIZE-aligned tile, so
// each workgroup loads its tile into groupshared and runs them there — collapsing those 8 steps into one
// dispatch. The ascending flag `(global & sequenceSize)` is UNIFORM across the tile: sequenceSize > GROUP_SIZE
// isolates a bit above the tile's low log2(GROUP_SIZE) bits, which are constant within a GROUP_SIZE-aligned tile
// (requires GROUP_SIZE to be a power of two, as the bitonic scheme already assumes), so it is computed once.
[numthreads(NWB_BVH_SORT_GROUP_SIZE, 1, 1)]
void nwbBvhSortGlobalTail(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID){
    const uint tileBase = groupID.x * NWB_BVH_SORT_GROUP_SIZE;
    const uint local = groupThreadID.x;
    const uint global = tileBase + local;
    const uint sequenceSize = g_NwbBvhBitonicSortPushConstants.sequenceSize;

    s_tileKeys[local] = g_NwbBvhSortKeys[global];
    s_tilePayload[local] = g_NwbBvhSortPayload[global];
    GroupMemoryBarrierWithGroupSync();

    // Intra-tile tail: compareDistance from GROUP_SIZE/2 down to 1 (all < GROUP_SIZE, so partner = local ^
    // compareDistance stays in this tile). Same compare-swap math as LOCAL_TILE and GLOBAL; the only difference
    // from LOCAL_TILE is the loop starts at GROUP_SIZE/2 (the inter-tile steps already handled GROUP_SIZE..seq/2)
    // and the ascending flag uses the GLOBAL sequenceSize rather than a local one.
    const bool ascending = (global & sequenceSize) == 0u;
    [unroll]
    for(uint compareDistance = NWB_BVH_SORT_GROUP_SIZE >> 1u; compareDistance > 0u; compareDistance >>= 1u){
        const uint partner = local ^ compareDistance;
        if(partner > local){
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

    g_NwbBvhSortKeys[global] = s_tileKeys[local];
    g_NwbBvhSortPayload[global] = s_tilePayload[local];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_BVH_SORT_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID){
    if(g_NwbBvhBitonicSortPushConstants.mode == 0u)
        nwbBvhSortLocalTile(groupID, groupThreadID);
    else if(g_NwbBvhBitonicSortPushConstants.mode == 1u)
        nwbBvhSortGlobalMerge(dispatchThreadID);
    else
        nwbBvhSortGlobalTail(groupID, groupThreadID);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
