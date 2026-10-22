// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "refraction/resolve_common.slangi"
#include "refraction_tile_probe_layout.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The test shares the production push ABI; the material-context selector carries its private visit buffer.
#define g_NwbRefractionProbeVisits g_NwbHeapStorageBuffers[g_NwbRefractionPush.materialContextSlotsHeapSlot]


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFRACTION_GROUP_SIZE, NWB_REFRACTION_GROUP_SIZE, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID){
    uint2 pixel;
    const bool accepted = nwbRefractionResolvePixel(groupId, groupThreadId, pixel);
    const uint queueIndex = groupId.y * NWB_REFRACTION_MAX_GROUP_COUNT_X + groupId.x;
    const uint count = g_NwbRefractionTileArguments.Load(NWB_REFRACTION_TILE_COUNTER_OFFSET);
    const bool firstLane = all(groupThreadId == uint3(0u));
    uint unused;
    if(firstLane)
        g_NwbRefractionProbeVisits.InterlockedAdd(NWB_SMOKE_REFRACTION_PROBE_GROUP_COUNT * 4u, 1u, unused);

    if(queueIndex >= count){
        if(firstLane)
            g_NwbRefractionProbeVisits.InterlockedAdd(NWB_SMOKE_REFRACTION_PROBE_TAIL_GROUP_COUNT * 4u, 1u, unused);
        if(accepted)
            g_NwbRefractionProbeVisits.InterlockedAdd(NWB_SMOKE_REFRACTION_PROBE_MAPPING_ERROR_COUNT * 4u, 1u, unused);
        return;
    }

    if(!accepted){
        if(firstLane)
            g_NwbRefractionProbeVisits.InterlockedAdd(NWB_SMOKE_REFRACTION_PROBE_MAPPING_ERROR_COUNT * 4u, 1u, unused);
        g_NwbRefractionProbeVisits.InterlockedAdd(NWB_SMOKE_REFRACTION_PROBE_EDGE_LANE_COUNT * 4u, 1u, unused);
        return;
    }
    if(pixel.x >= g_NwbRefractionPush.width || pixel.y >= g_NwbRefractionPush.height){
        g_NwbRefractionProbeVisits.InterlockedAdd(NWB_SMOKE_REFRACTION_PROBE_MAPPING_ERROR_COUNT * 4u, 1u, unused);
        return;
    }

    const uint tilesX = (g_NwbRefractionPush.width + NWB_REFRACTION_GROUP_SIZE - 1u) / NWB_REFRACTION_GROUP_SIZE;
    const uint tilesY = (g_NwbRefractionPush.height + NWB_REFRACTION_GROUP_SIZE - 1u) / NWB_REFRACTION_GROUP_SIZE;
    const uint tileCount = tilesX * tilesY;
    const uint tile = (pixel.y / NWB_REFRACTION_GROUP_SIZE) * tilesX + pixel.x / NWB_REFRACTION_GROUP_SIZE;
    if(firstLane)
        g_NwbRefractionProbeVisits.InterlockedAdd((NWB_SMOKE_REFRACTION_PROBE_HEADER_WORDS + tile) * 4u, 1u, unused);
    const uint pixelIndex = pixel.y * g_NwbRefractionPush.width + pixel.x;
    g_NwbRefractionProbeVisits.InterlockedAdd((NWB_SMOKE_REFRACTION_PROBE_HEADER_WORDS + tileCount + pixelIndex) * 4u, 1u, unused);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

