// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "bvh_heap_resources.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbBvhBitonicSortPushConstants{
    uint compareDistance;
    uint sequenceSize;
    uint mode;
    uint keysHeapSlot;
    uint payloadHeapSlot;
};

[[vk::push_constant]] ConstantBuffer<NwbBvhBitonicSortPushConstants> g_NwbBvhBitonicSortPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbBvhSortPairLess(const uint keyA, const uint payloadA, const uint keyB, const uint payloadB){
    if(keyA != keyB)
        return keyA < keyB;
    return payloadA < payloadB;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


groupshared uint g_NwbBvhSortTileKeys[NWB_BVH_SORT_GROUP_SIZE];
groupshared uint g_NwbBvhSortTilePayload[NWB_BVH_SORT_GROUP_SIZE];

#define NWB_BVH_SORT_TILE_COMPARE_SWAP(ASCENDING, PARTNER) \
    const uint keyA = g_NwbBvhSortTileKeys[local]; \
    const uint payloadA = g_NwbBvhSortTilePayload[local]; \
    const uint keyB = g_NwbBvhSortTileKeys[PARTNER]; \
    const uint payloadB = g_NwbBvhSortTilePayload[PARTNER]; \
    const bool aLessB = nwbBvhSortPairLess(keyA, payloadA, keyB, payloadB); \
    if((ASCENDING) != aLessB){ \
        g_NwbBvhSortTileKeys[local] = keyB; \
        g_NwbBvhSortTilePayload[local] = payloadB; \
        g_NwbBvhSortTileKeys[PARTNER] = keyA; \
        g_NwbBvhSortTilePayload[PARTNER] = payloadA; \
    }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_BVH_SORT_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID){
    const uint mode = g_NwbBvhBitonicSortPushConstants.mode;

    if(mode == NWB_BVH_SORT_MODE_GLOBAL){
        const uint compareDistance = g_NwbBvhBitonicSortPushConstants.compareDistance;
        const uint lowMask = compareDistance - 1u;
        const uint pairIndex = dispatchThreadID.x;
        const uint index = ((pairIndex & ~lowMask) << 1u) | (pairIndex & lowMask);
        const uint partner = index | compareDistance;

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

    const uint tileBase = groupID.x * NWB_BVH_SORT_GROUP_SIZE;
    const uint local = groupThreadID.x;
    const uint global = tileBase + local;

    g_NwbBvhSortTileKeys[local] = g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.keysHeapSlot][global];
    g_NwbBvhSortTilePayload[local] = g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.payloadHeapSlot][global];
    GroupMemoryBarrierWithGroupSync();

    if(mode == NWB_BVH_SORT_MODE_LOCAL_TILE){
        [unroll] for(uint sequenceSize = 2u; sequenceSize <= NWB_BVH_SORT_GROUP_SIZE; sequenceSize <<= 1u){
            [unroll] for(uint compareDistance = sequenceSize >> 1u; compareDistance > 0u; compareDistance >>= 1u){
                const uint partner = local ^ compareDistance;
                if(partner > local){
                    const bool ascending = (global & sequenceSize) == 0u;
                    NWB_BVH_SORT_TILE_COMPARE_SWAP(ascending, partner)
                }
                GroupMemoryBarrierWithGroupSync();
            }
        }
    }
    else{
        const uint sequenceSize = g_NwbBvhBitonicSortPushConstants.sequenceSize;
        const bool ascending = (global & sequenceSize) == 0u;
        [unroll] for(uint compareDistance = NWB_BVH_SORT_GROUP_SIZE >> 1u; compareDistance > 0u; compareDistance >>= 1u){
            const uint partner = local ^ compareDistance;
            if(partner > local){
                NWB_BVH_SORT_TILE_COMPARE_SWAP(ascending, partner)
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }

    g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.keysHeapSlot][global] = g_NwbBvhSortTileKeys[local];
    g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBitonicSortPushConstants.payloadHeapSlot][global] = g_NwbBvhSortTilePayload[local];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_BVH_SORT_TILE_COMPARE_SWAP


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

