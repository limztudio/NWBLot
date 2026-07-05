// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel spawn: one thread per screen tile (spawnTile x spawnTile px). Reads the tile-centre G-buffer world position +
// normal, walks the 3x3x3 neighbour cells of the PREVIOUS frame's hash to sum how well existing surfels cover the
// point, and -- if coverage is below threshold -- bump-allocates a new surfel (InterlockedAdd on an integer counter,
// no float atomics) initialised on that surface. On the BOOTSTRAP frame the hash is empty, so every visible tile
// spawns once -> all visible surfaces covered in one frame (the smoke app renders only the bootstrap frame when
// unfocused, so this is mandatory). Background pixels (no G-buffer normal) are skipped. Recycling / keep-alive land in
// U1. See .helper/surfel_gi_plan.md (Allocation).


#include "surfel_record.slangi"
#include "surfel_constants.slangi"
#include "surfel_hash.slangi"
#include "surfel_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_SURFEL_BINDING_CONSTANTS, NWB_SURFEL_SET)]]
ConstantBuffer<NwbSurfelConstants> g_NwbSurfelConstants;

[[vk::binding(NWB_SURFEL_BINDING_POOL, NWB_SURFEL_SET)]]
RWStructuredBuffer<NwbSurfel> g_NwbSurfelPool;

[[vk::binding(NWB_SURFEL_BINDING_CELL_HEAD, NWB_SURFEL_SET)]]
StructuredBuffer<uint> g_NwbSurfelCellHead;   // previous frame's hash (coverage query)

[[vk::binding(NWB_SURFEL_BINDING_COUNTER, NWB_SURFEL_SET)]]
RWStructuredBuffer<uint> g_NwbSurfelCounter;

[[vk::binding(NWB_SURFEL_BINDING_GBUFFER_WORLD_POSITION, NWB_SURFEL_SET)]]
Texture2D<float4> g_NwbSurfelGbufferWorldPosition;

[[vk::binding(NWB_SURFEL_BINDING_GBUFFER_NORMAL, NWB_SURFEL_SET)]]
Texture2D<float4> g_NwbSurfelGbufferNormal;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SURFEL_GROUP_SIZE, NWB_SURFEL_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint screenWidth = uint(g_NwbSurfelConstants.ageRaysTileScreen.w);
    const uint screenHeight = uint(g_NwbSurfelConstants.screenHeightPad.x);
    const uint spawnTile = max(uint(g_NwbSurfelConstants.ageRaysTileScreen.z), 1u);

    // One thread per spawn tile; sample the tile centre.
    const uint2 pixel = dispatchThreadID.xy * spawnTile + (spawnTile / 2u);
    if(pixel.x >= screenWidth || pixel.y >= screenHeight)
        return;

    const int3 loadCoord = int3(int2(pixel), 0);
    const float3 rawNormal = g_NwbSurfelGbufferNormal.Load(loadCoord).xyz;
    if(dot(rawNormal, rawNormal) < 0.01)
        return;   // no geometry at this tile (background)

    const float3 worldPosition = g_NwbSurfelGbufferWorldPosition.Load(loadCoord).xyz;
    const float3 worldNormal = normalize(rawNormal);

    const float cellSize = g_NwbSurfelConstants.cameraPositionCellSize.w;
    const uint cellCount = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.x);
    const uint poolCapacity = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.y);
    const uint frameIndex = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.z);
    const float coverageThreshold = g_NwbSurfelConstants.coverageRadiusBiasHyst.x;
    const float defaultRadius = g_NwbSurfelConstants.coverageRadiusBiasHyst.y;

    // Coverage: sum normal-aligned + distance-falloff weight over the 3x3x3 neighbour cells' surfels.
    float coverage = 0.0;
    const int3 baseCell = nwbSurfelCellCoord(worldPosition, cellSize);
    for(int dz = -1; dz <= 1; ++dz)
    for(int dy = -1; dy <= 1; ++dy)
    for(int dx = -1; dx <= 1; ++dx){
        const uint bucket = nwbSurfelHashCell(baseCell + int3(dx, dy, dz), cellCount);
        uint id = g_NwbSurfelCellHead[bucket];
        uint walk = 0u;
        [loop] while(id != NWB_SURFEL_CELL_INVALID && walk < NWB_SURFEL_MAX_WALK){
            const NwbSurfel s = g_NwbSurfelPool[id];
            if(s.alive != 0u){
                const float dist = length(worldPosition - s.position);
                const float normalWeight = saturate(dot(worldNormal, s.normal));
                coverage += normalWeight * saturate(1.0 - dist / max(s.radius, 1e-4));
            }
            id = s.nextInCell;
            ++walk;
        }
    }

    if(coverage < coverageThreshold){
        uint slot = 0u;
        InterlockedAdd(g_NwbSurfelCounter[NWB_SURFEL_COUNTER_BUMP_TOP], 1u, slot);
        if(slot < poolCapacity){
            NwbSurfel surfel;
            surfel.position = worldPosition;
            surfel.radius = defaultRadius;
            surfel.normal = worldNormal;
            surfel.nextInCell = NWB_SURFEL_CELL_INVALID;
            surfel.meanIrradiance = float3(0.0, 0.0, 0.0);
            surfel.sampleCount = 0u;
            surfel.lastSeenFrame = frameIndex;
            surfel.alive = 1u;
            surfel.pad0 = 0u;
            surfel.pad1 = 0u;
            g_NwbSurfelPool[slot] = surfel;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
