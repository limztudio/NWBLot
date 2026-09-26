// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel spawn: one thread per screen tile claims its EMPTY hash cell (atomic INVALID -> PENDING; losers skip) and
// bump-allocates one surfel. One surfel per bucket keeps the gather walk deterministic. Background tiles skipped.


#include "surfel_runtime.slangi"
#include "surfel_hash.slangi"
#include "surfel_gbuffer.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This frame's hash, allocation counters, and free-list are all StorageBuffer heap entries selected by the common
// dispatch push block. The aliases below keep the allocation logic uncluttered.
#define g_NwbSurfelCellHead g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.cellHeadHeapSlot]
#define g_NwbSurfelCounter g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.counterHeapSlot]
#define g_NwbSurfelFreeList g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.freeListHeapSlot]


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
    // The source G-buffer vectors are RGBA16F; the surfel pool and hash calculations below intentionally use FP32.
    const half3 rawNormalSample = half3(NwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.normalSlot).Load(loadCoord).xyz);
    const float3 rawNormal = float3(rawNormalSample);
    if(dot(rawNormal, rawNormal) < NWB_SURFEL_GBUFFER_NORMAL_LENGTH_SQUARED_MIN)
        return;   // no geometry at this tile (background)

    const half3 worldPositionSample = half3(NwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.worldPositionSlot).Load(loadCoord).xyz);
    const float3 worldPosition = float3(worldPositionSample);
    const float3 worldNormal = nwbSurfelDecodeGbufferNormal(rawNormal);

    const float cellSize = g_NwbSurfelConstants.cameraPositionCellSize.w;
    const uint cellCount = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.x);
    const uint poolCapacity = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.y);
    const uint frameIndex = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.z);
    const float defaultRadius = g_NwbSurfelConstants.coverageRadiusBiasHyst.y;

    // Collision-aware keep-alive: walk the bucket list for THIS tile's cell and refresh it (head-only refresh would
    // starve collided cells into flicker). id < poolCapacity terminates on INVALID / PENDING.
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

    // Atomic INVALID -> PENDING claim so one tile wins a contended empty bucket; losers (occupied or mid-claim) bail
    // without touching counters.
    uint previous = NWB_SURFEL_CELL_INVALID;
    InterlockedCompareExchange(g_NwbSurfelCellHead[bucket], NWB_SURFEL_CELL_INVALID, NWB_SURFEL_CELL_PENDING, previous);
    if(previous != NWB_SURFEL_CELL_INVALID)
        return;

    // Pop a recycled id first (pushes quiescent this pass, so no underflow); else capped bump-alloc (BUMP_TOP never
    // overshoots: undoing an over-bump could alias a live slot). On exhaustion release the claim for a later retry.
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

    // Spatial seed stops recycled surfels flashing dark: fresh surfels (sampleCount 0) are gather-skipped, so seed SH
    // from ESTABLISHED neighbours (sampleCount >= 2, never a this-frame spawn: no torn read, no seed-from-seed),
    // normal + distance weighted (SH average stays a valid field). First trace refines at weight 1/2.
    // Neighbour SH is previous-frame stable (pass 4 before this pass-3 spawn): race-free.
    half4 seedShR = half4(0.0h, 0.0h, 0.0h, 0.0h);
    half4 seedShG = half4(0.0h, 0.0h, 0.0h, 0.0h);
    half4 seedShB = half4(0.0h, 0.0h, 0.0h, 0.0h);
    half seedWeight = half(0.0h);
    for(int dz = -NWB_SURFEL_SEED_CELL_EXTENT; dz <= NWB_SURFEL_SEED_CELL_EXTENT; ++dz)
    for(int dy = -NWB_SURFEL_SEED_CELL_EXTENT; dy <= NWB_SURFEL_SEED_CELL_EXTENT; ++dy)
    for(int dx = -NWB_SURFEL_SEED_CELL_EXTENT; dx <= NWB_SURFEL_SEED_CELL_EXTENT; ++dx){
        const uint seedBucket = nwbSurfelHashCell(myCell + int3(dx, dy, dz), cellCount);
        uint seedId = g_NwbSurfelCellHead[seedBucket];
        uint seedWalk = 0u;
        [loop] while(seedId < poolCapacity && seedWalk < NWB_SURFEL_MAX_WALK){
            const NwbSurfel neighbour = g_NwbSurfelPool[seedId];
            if(neighbour.alive != 0u && neighbour.sampleCount >= NWB_SURFEL_SEED_NEIGHBOR_MIN_SAMPLE_COUNT){
                const float dist = length(worldPosition - neighbour.position);
                // Seed weights are (0, 1) bounded factors, so half arithmetic is sufficient here.
                const half normalWeight = half(saturate(dot(worldNormal, neighbour.normal)));
                const half distanceWeight = half(saturate(1.0 - dist / max(neighbour.radius, NWB_SURFEL_RADIUS_MIN)));
                const half weight = normalWeight * distanceWeight;
                seedShR += half4(neighbour.shR) * weight;
                seedShG += half4(neighbour.shG) * weight;
                seedShB += half4(neighbour.shB) * weight;
                seedWeight += weight;
            }
            seedId = neighbour.nextInCell;
            ++seedWalk;
        }
    }
    const bool seeded = seedWeight > half(NWB_SURFEL_GATHER_WEIGHT_EPSILON);
    const half seedNorm = seeded ? (half(1.0h) / seedWeight) : half(0.0h);

    NwbSurfel surfel;
    surfel.position = worldPosition;
    surfel.radius = defaultRadius;
    surfel.normal = worldNormal;
    surfel.nextInCell = NWB_SURFEL_CELL_INVALID;   // sole member of this cell's list
    surfel.shR = float4(seedShR * seedNorm);
    surfel.shG = float4(seedShG * seedNorm);
    surfel.shB = float4(seedShB * seedNorm);
    surfel.sampleCount = seeded ? 1u : 0u;
    surfel.lastSeenFrame = frameIndex;
    surfel.alive = 1u;
    surfel.pad0 = 0u;
    g_NwbSurfelPool[slot] = surfel;
    g_NwbSurfelCellHead[bucket] = slot;   // publish the real slot (replaces the PENDING sentinel), linking it into the cell
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

