// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "bvh_heap_resources.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// LOCAL_TILE sorts tiles, GLOBAL merges tiles, and GLOBAL_TAIL finishes tile-local merges.
// Keep groupshared sorting inline: the bundled slangc crashes on nested barrier networks in callees.


struct NwbBvhBitonicSortPushConstants{
    uint elementCount;       // real element count (tail beyond it holds the 0xFFFFFFFF sentinel padding)
    uint compareDistance;    // GLOBAL only: XOR distance for this merge step
    uint sequenceSize;       // GLOBAL / GLOBAL_TAIL only: current bitonic sequence size (a power of two)
    uint mode;               // NWB_BVH_SORT_MODE_*
    uint keysHeapSlot;
    uint payloadHeapSlot;
    uint2 pad;
};

[[vk::push_constant]]
ConstantBuffer<NwbBvhBitonicSortPushConstants> g_NwbBvhBitonicSortPushConstants;

// Payload breaks equal Morton-code ties for stable BVH topology.
bool nwbBvhSortPairLess(const uint keyA, const uint payloadA, const uint keyB, const uint payloadB){
    if(keyA != keyB)
        return keyA < keyB;
    return payloadA < payloadB;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


groupshared uint s_tileKeys[NWB_BVH_SORT_GROUP_SIZE];
groupshared uint s_tilePayload[NWB_BVH_SORT_GROUP_SIZE];

// Preprocessor-shared compare/swap keeps the inline tile modes aligned.
#define NWB_BVH_SORT_TILE_COMPARE_SWAP(ASCENDING, PARTNER) \
    const uint keyA = s_tileKeys[local]; \
    const uint payloadA = s_tilePayload[local]; \
    const uint keyB = s_tileKeys[PARTNER]; \
    const uint payloadB = s_tilePayload[PARTNER]; \
    const bool aLessB = nwbBvhSortPairLess(keyA, payloadA, keyB, payloadB); \
    if((ASCENDING) != aLessB){ \
        s_tileKeys[local] = keyB; \
        s_tilePayload[local] = payloadB; \
        s_tileKeys[PARTNER] = keyA; \
        s_tilePayload[PARTNER] = payloadA; \
    }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_BVH_SORT_GROUP_SIZE, 1, 1)]
void main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupID : SV_GroupID,
    uint3 groupThreadID : SV_GroupThreadID
){
    const uint mode = g_NwbBvhBitonicSortPushConstants.mode;

    if(mode == NWB_BVH_SORT_MODE_GLOBAL){
        // Inter-tile global-memory compare/swap.
        const uint index = dispatchThreadID.x;
        const uint partner = index ^ g_NwbBvhBitonicSortPushConstants.compareDistance;
        if(partner <= index)
            return;

        const uint keyA = g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.keysHeapSlot][index];
        const uint payloadA = g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.payloadHeapSlot][index];
        const uint keyB = g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.keysHeapSlot][partner];
        const uint payloadB = g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.payloadHeapSlot][partner];

        const bool ascending = (index & g_NwbBvhBitonicSortPushConstants.sequenceSize) == 0u;
        const bool aLessB = nwbBvhSortPairLess(keyA, payloadA, keyB, payloadB);
        if(ascending != aLessB){
            g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.keysHeapSlot][index] = keyB;
            g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.payloadHeapSlot][index] = payloadB;
            g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.keysHeapSlot][partner] = keyA;
            g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.payloadHeapSlot][partner] = payloadA;
        }
        return;
    }

    // Tile-local modes use groupshared storage.
    const uint tileBase = groupID.x * NWB_BVH_SORT_GROUP_SIZE;
    const uint local = groupThreadID.x;
    const uint global = tileBase + local;

    s_tileKeys[local] = g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.keysHeapSlot][global];
    s_tilePayload[local] = g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.payloadHeapSlot][global];
    GroupMemoryBarrierWithGroupSync();

    if(mode == NWB_BVH_SORT_MODE_LOCAL_TILE){
        [unroll]
        for(uint sequenceSize = 2u; sequenceSize <= NWB_BVH_SORT_GROUP_SIZE; sequenceSize <<= 1u){
            [unroll]
            for(uint compareDistance = sequenceSize >> 1u; compareDistance > 0u; compareDistance >>= 1u){
                const uint partner = local ^ compareDistance;
                if(partner > local){
                    // Use global index so tiles alternate sort direction.
                    const bool ascending = (global & sequenceSize) == 0u;
                    NWB_BVH_SORT_TILE_COMPARE_SWAP(ascending, partner)
                }
                GroupMemoryBarrierWithGroupSync();
            }
        }
    }
    else{
        // Finish the post-global tile-local merge tail.
        const uint sequenceSize = g_NwbBvhBitonicSortPushConstants.sequenceSize;
        const bool ascending = (global & sequenceSize) == 0u;
        [unroll]
        for(uint compareDistance = NWB_BVH_SORT_GROUP_SIZE >> 1u; compareDistance > 0u; compareDistance >>= 1u){
            const uint partner = local ^ compareDistance;
            if(partner > local){
                NWB_BVH_SORT_TILE_COMPARE_SWAP(ascending, partner)
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }

    g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.keysHeapSlot][global] = s_tileKeys[local];
    g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.payloadHeapSlot][global] = s_tilePayload[local];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_BVH_SORT_TILE_COMPARE_SWAP


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

