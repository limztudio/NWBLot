// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The preceding classifier dispatch publishes its counter before this one-group selection.
// Exactly one indirect record is active; the dense kernel never reads the compact queue.
[numthreads(1, 1, 1)]
void main(){
    const uint tilesX = (g_NwbRefractionPush.width + NWB_REFRACTION_GROUP_SIZE - 1u) / NWB_REFRACTION_GROUP_SIZE;
    const uint tilesY = (g_NwbRefractionPush.height + NWB_REFRACTION_GROUP_SIZE - 1u) / NWB_REFRACTION_GROUP_SIZE;
    const uint count = g_NwbRefractionTileArguments.Load(NWB_REFRACTION_TILE_COUNTER_OFFSET);
    const bool dense = count == tilesX * tilesY && tilesX <= NWB_REFRACTION_MAX_GROUP_COUNT_X;
    const uint compactY = max(1u, (count + NWB_REFRACTION_MAX_GROUP_COUNT_X - 1u) / NWB_REFRACTION_MAX_GROUP_COUNT_X);
    g_NwbRefractionTileArguments.Store(0u, dense ? 0u : min(count, NWB_REFRACTION_MAX_GROUP_COUNT_X));
    g_NwbRefractionTileArguments.Store(4u, dense ? 1u : compactY);
    g_NwbRefractionTileArguments.Store(8u, 1u);
    g_NwbRefractionTileArguments.Store(NWB_REFRACTION_DENSE_ARGUMENT_OFFSET, dense ? tilesX : 0u);
    g_NwbRefractionTileArguments.Store(NWB_REFRACTION_DENSE_ARGUMENT_OFFSET + 4u, dense ? tilesY : 1u);
    g_NwbRefractionTileArguments.Store(NWB_REFRACTION_DENSE_ARGUMENT_OFFSET + 8u, 1u);
    g_NwbRefractionTileArguments.Store(NWB_REFRACTION_DENSE_ARGUMENT_OFFSET + 12u, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

