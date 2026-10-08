// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel spawn: screen tiles retain compatible surface representatives or insert into a bounded hash-bucket list.
// A one-shot bucket claim serializes insertion; contenders retry in later frames instead of spinning on the GPU.


#define NWB_SURFEL_SPAWN_COHERENT_ACCESS
#include "surfel_runtime.slangi"
#undef NWB_SURFEL_SPAWN_COHERENT_ACCESS
#include "surfel_hash.slangi"
#include "surfel_gbuffer.slangi"
#include "surfel_spawn_policy.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This frame's hash, allocation counters, and free-list are all StorageBuffer heap entries selected by the common dispatch push block. The aliases below keep the allocation logic uncluttered.
#define g_NwbSurfelCellHead g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.cellHeadHeapSlot]
#define g_NwbSurfelCounter g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.counterHeapSlot]
#define g_NwbSurfelFreeList g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.freeListHeapSlot]
#define g_NwbSurfelSeedPool g_NwbHeapSurfelReadOnlyPools[g_NwbSurfelHeapPushConstants.snapshotPoolHeapSlot]
#define g_NwbSurfelSeedCellHead g_NwbHeapSurfelReadOnlyUintBuffers[g_NwbSurfelHeapPushConstants.snapshotCellHeadHeapSlot]


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbSurfelFindSpawnCoverage(
    uint id,
    const uint poolCapacity,
    const int3 cell,
    const float cellSize,
    const float3 worldPosition,
    const float3 worldNormal,
    const float normalBias,
    out uint walk
){
    walk = 0u;
    [loop]
    while(id < poolCapacity && walk < NWB_SURFEL_MAX_WALK){
        ++walk;
        const float3 position = g_NwbSurfelPool[id].position;
        const float3 normal = g_NwbSurfelPool[id].normal;
        const float3 delta = worldPosition - position;
        const float planeDistance = max(abs(dot(delta, normal)), abs(dot(delta, worldNormal)));
        if(g_NwbSurfelPool[id].alive != 0u && nwbSurfelSpawnCoversCandidate(
            all(nwbSurfelCellCoord(position, cellSize) == cell),
            dot(worldNormal, normal),
            dot(delta, delta),
            planeDistance,
            g_NwbSurfelPool[id].radius,
            normalBias
        ))
            return id;
        id = g_NwbSurfelPool[id].nextInCell;
    }
    return NWB_SURFEL_CELL_INVALID;
}

// Every caller owns the bucket claim. Release initialized record data before making the real head visible.
void nwbSurfelPublishSpawnHead(const uint bucket, const uint head){
    DeviceMemoryBarrier();
    InterlockedExchange(g_NwbSurfelCellHead[bucket], head);
}


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
    const half3 rawNormalSample = half3(nwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.normalSlot).Load(loadCoord).xyz);
    const float3 rawNormal = float3(rawNormalSample);
    if(dot(rawNormal, rawNormal) < NWB_SURFEL_GBUFFER_NORMAL_LENGTH_SQUARED_MIN)
        return; // no geometry at this tile (background)

    const half3 worldPositionSample = half3(nwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.worldPositionSlot).Load(loadCoord).xyz);
    const float3 worldPosition = float3(worldPositionSample);
    const float3 worldNormal = float3(nwbSurfelDecodeGbufferNormal(rawNormalSample));

    const float cellSize = g_NwbSurfelConstants.cameraPositionCellSize.w;
    const uint cellCount = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.x);
    const uint poolCapacity = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.y);
    const uint frameIndex = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.z);
    const float defaultRadius = g_NwbSurfelConstants.coverageRadiusBiasHyst.y;
    const float normalBias = g_NwbSurfelConstants.coverageRadiusBiasHyst.z;

    const int3 myCell = nwbSurfelCellCoord(worldPosition, cellSize);
    const uint bucket = nwbSurfelHashCell(myCell, cellCount);
    uint oldHead;
    InterlockedOr(g_NwbSurfelCellHead[bucket], 0u, oldHead);
    if(oldHead >= poolCapacity && oldHead != NWB_SURFEL_CELL_INVALID)
        return; // PENDING belongs to another insertion; never acquire PENDING -> PENDING.

    // Pair the atomic head observation with the publisher's barrier before reading its immutable record fields.
    // Covered candidates stop contending, allowing other faces in the same bucket to acquire a later frame's claim.
    DeviceMemoryBarrier();
    uint walk;
    uint covered = nwbSurfelFindSpawnCoverage(oldHead, poolCapacity, myCell, cellSize, worldPosition, worldNormal, normalBias, walk);
    if(covered != NWB_SURFEL_CELL_INVALID){
        InterlockedExchange(g_NwbSurfelPool[covered].lastSeenFrame, frameIndex);
        return;
    }

    // Claim either an empty or occupied bucket once. A changed head means another candidate made progress.
    uint previous = NWB_SURFEL_CELL_INVALID;
    InterlockedCompareExchange(g_NwbSurfelCellHead[bucket], oldHead, NWB_SURFEL_CELL_PENDING, previous);
    if(previous != oldHead)
        return;
    DeviceMemoryBarrier();
    covered = nwbSurfelFindSpawnCoverage(oldHead, poolCapacity, myCell, cellSize, worldPosition, worldNormal, normalBias, walk);
    if(covered != NWB_SURFEL_CELL_INVALID){
        InterlockedExchange(g_NwbSurfelPool[covered].lastSeenFrame, frameIndex);
        nwbSurfelPublishSpawnHead(bucket, oldHead);
        return;
    }
    if(walk >= NWB_SURFEL_MAX_WALK){
        nwbSurfelPublishSpawnHead(bucket, oldHead); // Keep every admitted node reachable by the bounded gather.
        return;
    }

    // Pop a recycled id first (pushes quiescent this pass, so no underflow); else capped bump-alloc (BUMP_TOP never overshoots: undoing an over-bump could alias a live slot). On exhaustion release the claim for a later retry.
    uint slot = 0u;
    bool allocated = false;
    {
        uint freeTop = g_NwbSurfelCounter[NWB_SURFEL_COUNTER_FREE_TOP];
        [loop]
        while(freeTop != 0u){
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
        [loop]
        while(bumpTop < poolCapacity){
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
        nwbSurfelPublishSpawnHead(bucket, oldHead); // Pool full: preserve all existing surfaces in the bucket.
        return;
    }

    // Seed only from the immutable frame-start snapshot. Live neighbours may be locked or newly published by other spawn groups; the snapshot also prevents seed-from-seed feedback. Exclude records expiring in this frame.
    half4 seedShR = half4(0.0h, 0.0h, 0.0h, 0.0h);
    half4 seedShG = half4(0.0h, 0.0h, 0.0h, 0.0h);
    half4 seedShB = half4(0.0h, 0.0h, 0.0h, 0.0h);
    half seedWeight = half(0.0h);
    for(int dz = -NWB_SURFEL_SEED_CELL_EXTENT; dz <= NWB_SURFEL_SEED_CELL_EXTENT; ++dz)
    for(int dy = -NWB_SURFEL_SEED_CELL_EXTENT; dy <= NWB_SURFEL_SEED_CELL_EXTENT; ++dy)
    for(int dx = -NWB_SURFEL_SEED_CELL_EXTENT; dx <= NWB_SURFEL_SEED_CELL_EXTENT; ++dx){
        const uint seedBucket = nwbSurfelHashCell(myCell + int3(dx, dy, dz), cellCount);
        uint seedId = g_NwbSurfelSeedCellHead[seedBucket];
        uint seedWalk = 0u;
        [loop]
        while(seedId < poolCapacity && seedWalk < NWB_SURFEL_MAX_WALK){
            const NwbSurfel neighbour = g_NwbSurfelSeedPool[seedId];
            if(neighbour.alive != 0u && neighbour.sampleCount >= NWB_SURFEL_SEED_NEIGHBOR_MIN_SAMPLE_COUNT && frameIndex - neighbour.lastSeenFrame <= uint(g_NwbSurfelConstants.ageRaysTileScreen.x)){
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
    surfel.nextInCell = oldHead;
    surfel.shR = float4(seedShR * seedNorm);
    surfel.shG = float4(seedShG * seedNorm);
    surfel.shB = float4(seedShB * seedNorm);
    surfel.sampleCount = seeded ? 1u : 0u;
    surfel.lastSeenFrame = frameIndex;
    surfel.alive = 1u;
    surfel.pad0 = 0u;
    // A recycled slot must forget its old directions. Initialize its guide before publishing the new core slot.
    [unroll]
    for(uint guideLane = 0u; guideLane < NWB_SURFEL_GUIDE_FLOAT4S_PER_SURFEL; ++guideLane)
        g_NwbSurfelGuidePool[slot * NWB_SURFEL_GUIDE_FLOAT4S_PER_SURFEL + guideLane] = float4(NWB_SURFEL_GUIDE_INITIAL_WEIGHT);
    g_NwbSurfelPool[slot] = surfel;
    nwbSurfelPublishSpawnHead(bucket, slot);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

