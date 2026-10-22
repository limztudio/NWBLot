// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


groupshared uint g_NwbRefractionTileActive;

// Capture already evaluated every optical surface at full resolution.
// OR its depth validity across the tile; alpha and low-resolution texture samples cannot hide clear glass.
[numthreads(NWB_REFRACTION_GROUP_SIZE, NWB_REFRACTION_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex){
    if(groupIndex == 0u)
        g_NwbRefractionTileActive = 0u;
    GroupMemoryBarrierWithGroupSync();

    const uint2 pixel = dispatchThreadId.xy;
    if(pixel.x < g_NwbRefractionPush.width && pixel.y < g_NwbRefractionPush.height){
        const float depth = nwbHeapSampledImage2D(g_NwbRefractionResources.refractionSlots0.x).Load(int3(pixel, 0)).r;
        if(depth < 1.0)
            InterlockedOr(g_NwbRefractionTileActive, 1u);
    }
    GroupMemoryBarrierWithGroupSync();

    if(groupIndex != 0u)
        return;
    if(all(groupId.xy == uint2(0u))){
        uint unused;
        g_NwbRefractionTileArguments.InterlockedMax(4u, 1u, unused);
        g_NwbRefractionTileArguments.Store(8u, 1u);
    }
    if(g_NwbRefractionTileActive == 0u)
        return;

    uint queueIndex;
    g_NwbRefractionTileArguments.InterlockedAdd(NWB_REFRACTION_TILE_COUNTER_OFFSET, 1u, queueIndex);
    const uint tilesX = (g_NwbRefractionPush.width + NWB_REFRACTION_GROUP_SIZE - 1u) / NWB_REFRACTION_GROUP_SIZE;
    g_NwbRefractionTileList.Store(queueIndex * 4u, groupId.y * tilesX + groupId.x);

    // Monotonic maxima let the classifier publish complete indirect arguments in the same dispatch.
    // The graph's UAV-to-indirect barrier precedes their only consumer.
    uint unused;
    g_NwbRefractionTileArguments.InterlockedMax(0u, min(queueIndex + 1u, NWB_REFRACTION_MAX_GROUP_COUNT_X), unused);
    g_NwbRefractionTileArguments.InterlockedMax(4u, queueIndex / NWB_REFRACTION_MAX_GROUP_COUNT_X + 1u, unused);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

