// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SURFEL_SPAWN_COHERENT_ACCESS
#include "surfel_runtime.slangi"
#undef NWB_SURFEL_SPAWN_COHERENT_ACCESS
#include "surfel_hash.slangi"
#include "surfel_gbuffer.slangi"
#include "surfel_spawn_policy.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbSurfelCellHead g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.cellHeadHeapSlot]
#define g_NwbSurfelCounter g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.counterHeapSlot]
#define g_NwbSurfelFreeList g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.freeListHeapSlot]
#define g_NwbSurfelSeedPool g_NwbHeapSurfelReadOnlyPools[g_NwbSurfelHeapPushConstants.snapshotPoolHeapSlot]
#define g_NwbSurfelSeedCellHead g_NwbHeapSurfelReadOnlyUintBuffers[g_NwbSurfelHeapPushConstants.snapshotCellHeadHeapSlot]


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbSurfelFindSpawnCoverage(uint id, const uint poolCapacity, const int3 cell, const float radius, const float3 worldPosition, const float3 worldNormal, const float normalBias, out uint walk){
    walk = 0u;
    [loop] while(id < poolCapacity && walk < NWB_SURFEL_MAX_WALK){
        ++walk;
        const float3 position = g_NwbSurfelPool[id].position;
        const float3 normal = g_NwbSurfelPool[id].normal;
        const float3 delta = worldPosition - position;
        const float planeDistance = max(abs(dot(delta, normal)), abs(dot(delta, worldNormal)));
        if(nwbSurfelIsAlive(g_NwbSurfelPool[id].sampleState) && nwbSurfelSpawnCoversCandidate(nwbSurfelSameCell(g_NwbSurfelPool[id].cell, cell), dot(worldNormal, normal), dot(delta, delta), planeDistance, radius, normalBias))
            return id;
        id = g_NwbSurfelPool[id].nextInCell;
    }
    return NWB_SURFEL_CELL_INVALID;
}

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

    const uint2 pixel = dispatchThreadID.xy * spawnTile + (spawnTile / 2u);
    if(pixel.x >= screenWidth || pixel.y >= screenHeight)
        return;

    const int3 loadCoord = int3(int2(pixel), 0);
    const half3 rawNormalSample = half3(nwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.normalSlot).Load(loadCoord).xyz);
    const float3 rawNormal = float3(rawNormalSample);
    if(dot(rawNormal, rawNormal) < NWB_SURFEL_GBUFFER_NORMAL_LENGTH_SQUARED_MIN)
        return;

    const half3 worldPositionSample = half3(nwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.worldPositionSlot).Load(loadCoord).xyz);
    const float3 worldPosition = float3(worldPositionSample);
    const float3 worldNormal = float3(nwbSurfelDecodeGbufferNormal(rawNormalSample));

    const float cellSize = g_NwbSurfelConstants.cellSizeRadiusBiasAccum.x;
    const uint cellCount = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.x);
    const uint poolCapacity = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.y);
    const uint frameIndex = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.z);
    const float defaultRadius = g_NwbSurfelConstants.cellSizeRadiusBiasAccum.y;
    const float normalBias = g_NwbSurfelConstants.cellSizeRadiusBiasAccum.z;

    const int3 myCell = nwbSurfelCellCoord(worldPosition, cellSize);
    const uint bucket = nwbSurfelHashCell(myCell, cellCount);
    uint oldHead;
    InterlockedOr(g_NwbSurfelCellHead[bucket], 0u, oldHead);
    if(oldHead >= poolCapacity && oldHead != NWB_SURFEL_CELL_INVALID)
        return;

    DeviceMemoryBarrier();
    uint walk;
    uint covered = nwbSurfelFindSpawnCoverage(oldHead, poolCapacity, myCell, defaultRadius, worldPosition, worldNormal, normalBias, walk);
    if(covered != NWB_SURFEL_CELL_INVALID){
        InterlockedExchange(g_NwbSurfelPool[covered].lastSeenFrame, frameIndex);
        return;
    }

    uint previous = NWB_SURFEL_CELL_INVALID;
    InterlockedCompareExchange(g_NwbSurfelCellHead[bucket], oldHead, NWB_SURFEL_CELL_PENDING, previous);
    if(previous != oldHead)
        return;
    DeviceMemoryBarrier();
    covered = nwbSurfelFindSpawnCoverage(oldHead, poolCapacity, myCell, defaultRadius, worldPosition, worldNormal, normalBias, walk);
    if(covered != NWB_SURFEL_CELL_INVALID){
        InterlockedExchange(g_NwbSurfelPool[covered].lastSeenFrame, frameIndex);
        nwbSurfelPublishSpawnHead(bucket, oldHead);
        return;
    }
    if(walk >= NWB_SURFEL_MAX_WALK){
        nwbSurfelPublishSpawnHead(bucket, oldHead);
        return;
    }

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
        nwbSurfelPublishSpawnHead(bucket, oldHead);
        return;
    }

    half4 seedShR = half4(0.0h, 0.0h, 0.0h, 0.0h);
    half4 seedShG = half4(0.0h, 0.0h, 0.0h, 0.0h);
    half4 seedShB = half4(0.0h, 0.0h, 0.0h, 0.0h);
    half seedWeight = half(0.0h);
    for(int dz = -NWB_SURFEL_SEED_CELL_EXTENT; dz <= NWB_SURFEL_SEED_CELL_EXTENT; ++dz)
    for(int dy = -NWB_SURFEL_SEED_CELL_EXTENT; dy <= NWB_SURFEL_SEED_CELL_EXTENT; ++dy)
    for(int dx = -NWB_SURFEL_SEED_CELL_EXTENT; dx <= NWB_SURFEL_SEED_CELL_EXTENT; ++dx){
        const int3 seedCell = myCell + int3(dx, dy, dz);
        const uint seedBucket = nwbSurfelHashCell(seedCell, cellCount);
        uint seedId = g_NwbSurfelSeedCellHead[seedBucket];
        uint seedWalk = 0u;
        [loop] while(seedId < poolCapacity && seedWalk < NWB_SURFEL_MAX_WALK){
            const uint nextInCell = g_NwbSurfelSeedPool[seedId].nextInCell;
            const uint neighbourState = g_NwbSurfelSeedPool[seedId].sampleState;
            const uint neighbourLastSeen = g_NwbSurfelSeedPool[seedId].lastSeenFrame;
            if(
                nwbSurfelIsAlive(neighbourState)
                && nwbSurfelSampleCount(neighbourState) >= NWB_SURFEL_SEED_NEIGHBOR_MIN_SAMPLE_COUNT
                && frameIndex - neighbourLastSeen <= uint(g_NwbSurfelConstants.ageRaysTileScreen.x)
                && nwbSurfelSameCell(g_NwbSurfelSeedPool[seedId].cell, seedCell)
            ){
                const float dist = length(worldPosition - g_NwbSurfelSeedPool[seedId].position);
                const half normalWeight = half(saturate(dot(worldNormal, g_NwbSurfelSeedPool[seedId].normal)));
                const half distanceWeight = half(saturate(1.0h - dist / max(defaultRadius, NWB_SURFEL_RADIUS_MIN)));
                const half weight = normalWeight * distanceWeight;
                if(weight > half(0.0h)){
                    seedShR += half4(g_NwbSurfelSeedPool[seedId].shR) * weight;
                    seedShG += half4(g_NwbSurfelSeedPool[seedId].shG) * weight;
                    seedShB += half4(g_NwbSurfelSeedPool[seedId].shB) * weight;
                    seedWeight += weight;
                }
            }
            seedId = nextInCell;
            ++seedWalk;
        }
    }
    const bool seeded = seedWeight > half(NWB_SURFEL_GATHER_WEIGHT_EPSILON);
    const half seedNorm = seeded ? (half(1.0h) / seedWeight) : half(0.0h);

    NwbSurfel surfel;
    surfel.position = worldPosition;
    surfel.nextInCell = oldHead;
    surfel.normal = worldNormal;
    surfel.sampleState = nwbSurfelLiveSampleState(seeded ? 1u : 0u);
    surfel.shR = float4(seedShR * seedNorm);
    surfel.shG = float4(seedShG * seedNorm);
    surfel.shB = float4(seedShB * seedNorm);
    surfel.cell = myCell;
    surfel.lastSeenFrame = frameIndex;
    [unroll]
    for(uint guideLane = 0u; guideLane < NWB_SURFEL_GUIDE_FLOAT4S_PER_SURFEL; ++guideLane)
        g_NwbSurfelGuidePool[slot * NWB_SURFEL_GUIDE_FLOAT4S_PER_SURFEL + guideLane] = float4(NWB_SURFEL_GUIDE_INITIAL_WEIGHT);
    g_NwbSurfelPool[slot] = surfel;
    nwbSurfelPublishSpawnHead(bucket, slot);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

