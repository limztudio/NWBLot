// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel spawn: one thread per screen tile (spawnTile x spawnTile px). Reads the tile-centre G-buffer world position +
// normal, hashes it to a cell, and -- if that cell's list is still EMPTY -- claims the cell and bump-allocates one
// surfel on that surface. ONE SURFEL PER HASH BUCKET: the hash is rebuilt from the live pool BEFORE this pass, so a
// non-empty cell head means a surfel already covers the cell (skip). Many screen tiles map to one near-camera cell on
// the bootstrap frame, so the empty-cell claim is atomic -- CompareExchange(INVALID -> PENDING) -- and only the winning
// tile allocates; losers skip (no wasted slots). Keeping every cell list length 1 is what makes the gather's fixed-order
// walk deterministic (the flicker was overstuffed cells walked in non-deterministic order + truncated). Background
// pixels (no G-buffer normal) are skipped. Recycling / keep-alive land in U1. See .helper/surfel_gi_plan.md (Allocation).


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
RWStructuredBuffer<uint> g_NwbSurfelCellHead;   // this frame's hash (built before spawn); the claim links the new surfel

[[vk::binding(NWB_SURFEL_BINDING_COUNTER, NWB_SURFEL_SET)]]
RWStructuredBuffer<uint> g_NwbSurfelCounter;

[[vk::binding(NWB_SURFEL_BINDING_FREE_LIST, NWB_SURFEL_SET)]]
RWStructuredBuffer<uint> g_NwbSurfelFreeList;   // recycled ids (U1); popped before bump-allocating

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
    const float defaultRadius = g_NwbSurfelConstants.coverageRadiusBiasHyst.y;

    // One surfel per hash bucket. The hash was rebuilt from the live pool BEFORE this pass. COLLISION-AWARE KEEP-ALIVE:
    // walk the bucket's (short) list and, if a node's cell matches THIS tile's cell, refresh its lastSeenFrame and return
    // -- the cell is covered. Refreshing only the list HEAD would starve a deeper node when two distinct cells collide
    // onto one bucket, ageing out a still-visible surfel = flicker; walking (bounded, like the gather) refreshes the
    // right one. id < poolCapacity terminates the walk on INVALID / PENDING (both > poolCapacity).
    const int3 myCell = nwbSurfelCellCoord(worldPosition, cellSize);
    const uint bucket = nwbSurfelHashCell(myCell, cellCount);
    {
        uint id = g_NwbSurfelCellHead[bucket];
        uint walk = 0u;
        [loop] while(id < poolCapacity && walk < NWB_SURFEL_MAX_WALK){
            if(all(nwbSurfelCellCoord(g_NwbSurfelPool[id].position, cellSize) == myCell)){
                g_NwbSurfelPool[id].lastSeenFrame = frameIndex;   // keep-alive (many tiles -> same node -> same value, benign)
                return;
            }
            id = g_NwbSurfelPool[id].nextInCell;
            ++walk;
        }
    }

    // No surfel covers this cell yet. Claim the bucket atomically so only ONE tile allocates when many map to the same
    // empty bucket (the bootstrap frame packs a near-camera cell with dozens of tiles). CompareExchange INVALID ->
    // PENDING: the winner gets INVALID back; everyone else (bucket occupied by a foreign-cell surfel, or another tile
    // mid-claim) gets a non-INVALID value and bails without touching the counters.
    uint previous = NWB_SURFEL_CELL_INVALID;
    InterlockedCompareExchange(g_NwbSurfelCellHead[bucket], NWB_SURFEL_CELL_INVALID, NWB_SURFEL_CELL_PENDING, previous);
    if(previous != NWB_SURFEL_CELL_INVALID)
        return;

    // Won the claim: allocate a slot. POP a recycled id off the free-list first (U1) via a CAS loop -- pushes are
    // quiescent this pass (age-free's push ran in a separate barrier-separated pass), so FREE_TOP only decreases and the
    // pop never underflows / never goes transiently negative. If the free-list is empty, BUMP-allocate via a CAS capped
    // at poolCapacity: BUMP_TOP is a monotonic high-water mark, so it must never overshoot -- decrementing it to "undo"
    // an over-bump could re-hand a slot index already holding a live surfel (aliasing). On pool exhaustion, release the
    // claim (restore INVALID) so a later frame retries once recycling frees slots.
    uint slot = 0u;
    bool allocated = false;
    {
        uint freeTop = g_NwbSurfelCounter[NWB_SURFEL_COUNTER_FREE_TOP];
        [loop] while(freeTop != 0u){
            uint prev;
            InterlockedCompareExchange(g_NwbSurfelCounter[NWB_SURFEL_COUNTER_FREE_TOP], freeTop, freeTop - 1u, prev);
            if(prev == freeTop){
                slot = g_NwbSurfelFreeList[freeTop - 1u];
                allocated = true;
                break;
            }
            freeTop = prev;
        }
    }
    if(!allocated){
        uint bumpTop = g_NwbSurfelCounter[NWB_SURFEL_COUNTER_BUMP_TOP];
        [loop] while(bumpTop < poolCapacity){
            uint prev;
            InterlockedCompareExchange(g_NwbSurfelCounter[NWB_SURFEL_COUNTER_BUMP_TOP], bumpTop, bumpTop + 1u, prev);
            if(prev == bumpTop){
                slot = bumpTop;
                allocated = true;
                break;
            }
            bumpTop = prev;
        }
    }
    if(!allocated){
        g_NwbSurfelCellHead[bucket] = NWB_SURFEL_CELL_INVALID;   // pool full -> release the claim
        return;
    }

    NwbSurfel surfel;
    surfel.position = worldPosition;
    surfel.radius = defaultRadius;
    surfel.normal = worldNormal;
    surfel.nextInCell = NWB_SURFEL_CELL_INVALID;   // sole member of this cell's list
    surfel.meanIrradiance = float3(0.0, 0.0, 0.0);
    surfel.sampleCount = 0u;
    surfel.lastSeenFrame = frameIndex;
    surfel.alive = 1u;
    surfel.pad0 = 0u;
    surfel.pad1 = 0u;
    g_NwbSurfelPool[slot] = surfel;
    g_NwbSurfelCellHead[bucket] = slot;   // publish the real slot (replaces the PENDING sentinel), linking it into the cell
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
