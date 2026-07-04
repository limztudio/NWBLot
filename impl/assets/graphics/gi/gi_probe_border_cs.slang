// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DDGI atlas border fill (U4). One thread per BORDER texel of every probe tile: copies the nearest interior edge
// texel outwards into the 1-texel border ring so octahedral sampling at the atlas edge does not wrap into the
// neighbour probe's texels (which would leak their irradiance / distance across the tile boundary). The border
// runs AFTER the blend on the same (now-front) atlas, so it operates on the atlas the consumer will sample.
//
// The irradiance + distance atlases have DIFFERENT tile sizes (8 vs 16) and DIFFERENT formats (RGBA16F vs RG16F),
// so this ONE pipeline is dispatched TWICE per frame: once with the irradiance border binding set (which binds the
// irradiance atlas at BOTH slots -- the distance slot is bound to the SAME irradiance atlas so the float4 write is
// always valid), and once with the distance border binding set (which binds the distance atlas at BOTH slots
// likewise). The push constant `mode` selects which slot the kernel reads/writes (0 = irradiance, 1 = distance).
// The dispatch grid is sized CPU-side to the bound atlas's tile geometry (probeCount * tile, tile). See
// .helper/ddgi_plan.md 2/3 (U4).
//
// Octahedral projection is continuous across the equator seams by construction, so the only wrap risk is the tile
// edge in atlas space -- clamping the border texel to its nearest interior neighbour preserves the octahedral
// continuity at the tile boundary.


#include "binding_slots.h"
#include "probe_grid.slangi"
#include "octahedral.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_GI_BORDER_BINDING_GRID_CONSTANTS, NWB_GI_SET)]]
ConstantBuffer<NwbGiGridConstants> g_NwbGiGrid;

[[vk::binding(NWB_GI_BORDER_BINDING_IRRADIANCE, NWB_GI_SET)]]
RWTexture2D<float4> g_NwbGiAtlas4;

[[vk::binding(NWB_GI_BORDER_BINDING_DISTANCE, NWB_GI_SET)]]
RWTexture2D<float2> g_NwbGiAtlas2;

struct NwbGiBorderPushConstants{
    uint tile;   // atlas tile edge length (NWB_GI_IRRADIANCE_ATLAS_TILE or NWB_GI_DISTANCE_ATLAS_TILE)
    uint mode;   // 0 = irradiance (float4 atlas), 1 = distance (float2 atlas)
    uint pad0;
    uint pad1;
};

[[vk::push_constant]]
ConstantBuffer<NwbGiBorderPushConstants> g_NwbGiBorderPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Resolves the source interior texel for one border texel (clamp the border coordinate into the interior extent).
uint2 nwbGiBorderSource(const uint2 localInTile, const uint tile){
    const uint lastInterior = tile - 2u; // interior runs [1, tile-2] in tile-local space
    uint2 src = localInTile;
    src.x = clamp(src.x, 1u, lastInterior);
    src.y = clamp(src.y, 1u, lastInterior);
    return src;
}


[numthreads(NWB_GI_GROUP_SIZE, NWB_GI_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint probeCount = nwbGiProbeCount(g_NwbGiGrid);
    const uint tile = g_NwbGiBorderPushConstants.tile;
    const uint mode = g_NwbGiBorderPushConstants.mode;
    if(tile == 0u)
        return;

    const uint atlasWidth = probeCount * tile;
    const uint texelX = dispatchThreadID.x;
    const uint texelY = dispatchThreadID.y;
    if(texelX >= atlasWidth || texelY >= tile)
        return;

    const uint probeIndex = texelX / tile;
    const uint localX = texelX % tile;
    const uint2 localInTile = uint2(localX, texelY);
    if(probeIndex >= probeCount)
        return;

    // Skip interior texels (the blend already wrote them).
    if(!nwbGiIsBorderTexel(localInTile, tile))
        return;

    // Clamp the border coordinate into the interior extent and copy.
    const uint2 srcLocal = nwbGiBorderSource(localInTile, tile);
    const uint2 atlasCoord = nwbGiAtlasTileOrigin(probeIndex, tile) + localInTile;
    const uint2 srcCoord = nwbGiAtlasTileOrigin(probeIndex, tile) + srcLocal;

    if(mode == 0u){
        g_NwbGiAtlas4[atlasCoord] = g_NwbGiAtlas4[srcCoord];
    }
    else{
        g_NwbGiAtlas2[atlasCoord] = g_NwbGiAtlas2[srcCoord];
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
