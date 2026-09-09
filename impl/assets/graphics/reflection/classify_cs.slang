// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.slangi"
#include "screen_trace.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_REFLECTION_CLASSIFY_GROUP_SIZE * NWB_REFLECTION_CLASSIFY_GROUP_SIZE != 64
#error "Reflection candidate compaction requires a 64-lane workgroup"
#endif
#if NWB_REFLECTION_FEEDBACK_GROUP_SIZE != NWB_REFLECTION_CLASSIFY_GROUP_SIZE
#error "Reflection feedback tiles must match classify workgroups"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Four seven-bit additive observation fields each hold at most 64 lanes without carrying into their neighbor.
static const uint s_NwbReflectionPotentialObservation = 1u;
static const uint s_NwbReflectionRoughObservation = 1u << 7u;
static const uint s_NwbReflectionAttemptObservation = 1u << 14u;
static const uint s_NwbReflectionReturnObservation = 1u << 21u;

// Shared integer operations do not require optional subgroup ballot or 64-bit atomic support. Screen work packs
// iterations in 16 bits (at most 128*256), then limit misses and bypassed pixels in 8 bits each (at most 128 each).
groupshared uint g_NwbReflectionCandidateMasks[4];
groupshared uint g_NwbReflectionCandidateBase;
groupshared uint g_NwbReflectionObservations[2];
groupshared uint g_NwbReflectionScreenWork;

struct NwbReflectionFeedbackTile{
    uint state;
    bool previousValid;
    bool bypass;
    bool probe;
};

NwbReflectionFeedbackTile nwbReflectionLoadFeedbackTile(const uint2 pixel, const bool glass){
    NwbReflectionFeedbackTile tile = (NwbReflectionFeedbackTile)0;
    if(
        !nwbReflectionFeedbackWriting()
        || (g_NwbReflectionParameters.feedbackFlags & NWB_REFLECTION_FEEDBACK_PREVIOUS_VALID) == 0u
    )
        return tile;
    const uint tilesX = (g_NwbReflectionParameters.dimensionsAndRoute.x + NWB_REFLECTION_FEEDBACK_GROUP_SIZE - 1u)
        / NWB_REFLECTION_FEEDBACK_GROUP_SIZE;
    const uint tilesY = (g_NwbReflectionParameters.dimensionsAndRoute.y + NWB_REFLECTION_FEEDBACK_GROUP_SIZE - 1u)
        / NWB_REFLECTION_FEEDBACK_GROUP_SIZE;
    // The CPU declares this previous-bank read only for a compatible accepted observation. Check the published
    // header before accessing tile entries; invalid selectors are never touched when the frame flag is absent.
    const uint4 header = NwbHeapRawBuffer(g_NwbReflectionParameters.feedbackReadSlot).Load4(0u);
    if(
        (header.w & NWB_REFLECTION_FEEDBACK_HEADER_VALID) == 0u || header.z != tilesX * tilesY
        || header.y != g_NwbReflectionParameters.feedbackProbeIndex - 1u
    )
        return tile;
    const uint tileIndex = (pixel.y / NWB_REFLECTION_FEEDBACK_GROUP_SIZE) * tilesX
        + pixel.x / NWB_REFLECTION_FEEDBACK_GROUP_SIZE;
    const uint entryIndex = tileIndex * NWB_REFLECTION_FEEDBACK_SURFACE_COUNT + (glass ? 1u : 0u);
    const uint entry = NwbHeapRawBuffer(g_NwbReflectionParameters.feedbackReadSlot).Load(
        NWB_REFLECTION_FEEDBACK_HEADER_BYTES + entryIndex * NWB_REFLECTION_FEEDBACK_ENTRY_BYTES
    );
    if(entry > NWB_REFLECTION_FEEDBACK_DORMANT)
        return tile;
    tile.state = entry;
    tile.previousValid = true;
    const uint budget = min(g_NwbReflectionParameters.maxHardwareRays, g_NwbReflectionParameters.queueCapacity);
    // Reserving groups can arrive in a different order when SSR is skipped. Every potential receiver must fit,
    // including other rough pixels whose current VNDF sample may differ from the last accepted frame.
    if(entry == NWB_REFLECTION_FEEDBACK_DORMANT && header.x <= budget){
        const uint phase = nwbReflectionSamplingHash(entryIndex) & (NWB_REFLECTION_FEEDBACK_PROBE_PERIOD - 1u);
        tile.probe = (g_NwbReflectionParameters.feedbackProbeIndex & (NWB_REFLECTION_FEEDBACK_PROBE_PERIOD - 1u)) == phase;
        tile.bypass = !tile.probe;
    }
    return tile;
}

uint nwbReflectionNextFeedbackState(const NwbReflectionFeedbackTile tile, const uint observations){
    const uint potential = observations & 127u;
    const uint rough = (observations >> 7u) & 127u;
    const uint attempts = (observations >> 14u) & 127u;
    const uint returns = (observations >> 21u) & 127u;
    if(potential == 0u || rough != 0u || returns != 0u)
        return NWB_REFLECTION_FEEDBACK_ACTIVE;
    if(attempts == potential){
        const uint previous = tile.previousValid ? tile.state : NWB_REFLECTION_FEEDBACK_ACTIVE;
        return min(previous + 1u, NWB_REFLECTION_FEEDBACK_DORMANT);
    }
    if(tile.bypass && attempts == 0u)
        return NWB_REFLECTION_FEEDBACK_DORMANT;
    return NWB_REFLECTION_FEEDBACK_ACTIVE;
}

bool nwbReflectionClassifySurface(
    const uint2 pixel,
    const bool glass,
    const NwbReflectionFeedbackTile tile,
    out float4 result,
    out uint observations,
    out uint screenWork){
    result = float4(0.0);
    observations = 0u;
    screenWork = 0u;
    if(g_NwbReflectionParameters.dimensionsAndRoute.z == NWB_REFLECTION_MODE_DISABLED)
        return false;
    NwbReflectionSurface surface;
    bool stableTraceEligible;
    const bool valid = nwbReflectionLoadSurface(pixel, glass, surface, stableTraceEligible);
    if(stableTraceEligible){
        observations = s_NwbReflectionPotentialObservation;
        if(!glass && surface.roughness > 0.0)
            observations |= s_NwbReflectionRoughObservation;
    }
    if(!valid)
        return false;
    nwbReflectionRecordDiagnostic(
        glass ? NWB_REFLECTION_COUNTER_GLASS_PIXELS : NWB_REFLECTION_COUNTER_OPAQUE_PIXELS, 1u
    );
    if(!any(surface.weight > 0.0)){
        // Below-horizon VNDF samples remain true zeros, but still contributed to stable eligibility above.
        result.a = 1.0;
        return false;
    }
    const float3 environment = nwbReflectionEnvironment(surface.direction);
    result = float4(surface.weight * environment, 1.0);
    const bool screen = g_NwbReflectionParameters.dimensionsAndRoute.z == NWB_REFLECTION_MODE_SCREEN
        || g_NwbReflectionParameters.dimensionsAndRoute.z == NWB_REFLECTION_MODE_HYBRID;
    if(screen && stableTraceEligible){
        if(tile.bypass && surface.roughness == 0.0)
            screenWork = 1u << 24u;
        else{
            observations |= s_NwbReflectionAttemptObservation;
            nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_SCREEN_ATTEMPTS, 1u);
            float3 radiance;
            float confidence;
            uint iterations;
            bool limited;
            const bool returned = nwbReflectionTraceScreen(pixel, surface, radiance, confidence, iterations, limited);
            screenWork = iterations | (limited ? 1u << 16u : 0u);
            if(returned){
                observations |= s_NwbReflectionReturnObservation;
                // Even provisional low-confidence radiance keeps the tile active. Hardware replaces it only
                // through the ordinary fallback route; feedback never drops a useful screen return.
                result = float4(surface.weight * lerp(environment, radiance, confidence), 2.0 + 0.5 * confidence);
                if(confidence >= g_NwbReflectionParameters.screenConfidenceThreshold){
                    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_SCREEN_HITS, 1u);
                    return false;
                }
            }
        }
    }
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_FALLBACK_PIXELS, 1u);
    const bool hardware = g_NwbReflectionParameters.dimensionsAndRoute.w != 0u
        && (g_NwbReflectionParameters.dimensionsAndRoute.z == NWB_REFLECTION_MODE_HARDWARE
            || g_NwbReflectionParameters.dimensionsAndRoute.z == NWB_REFLECTION_MODE_HYBRID);
    return hardware && stableTraceEligible;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFLECTION_CLASSIFY_GROUP_SIZE, NWB_REFLECTION_CLASSIFY_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex){
    const uint2 pixel = dispatchThreadId.xy;
    const bool hardware = g_NwbReflectionParameters.dimensionsAndRoute.w != 0u
        && (g_NwbReflectionParameters.dimensionsAndRoute.z == NWB_REFLECTION_MODE_HARDWARE
            || g_NwbReflectionParameters.dimensionsAndRoute.z == NWB_REFLECTION_MODE_HYBRID);
    const bool diagnostics = g_NwbReflectionParameters.diagnosticsEnabled != 0u;
    const bool feedbackWrites = nwbReflectionFeedbackWriting();
    const bool observe = diagnostics || feedbackWrites;
    const bool sharedWork = hardware || observe;
    if(hardware && groupIndex < 4u)
        g_NwbReflectionCandidateMasks[groupIndex] = 0u;
    if(observe && groupIndex < 2u)
        g_NwbReflectionObservations[groupIndex] = 0u;
    if(diagnostics && groupIndex == 0u)
        g_NwbReflectionScreenWork = 0u;
    bool opaqueCandidate = false;
    bool glassCandidate = false;
    uint opaqueObservations = 0u;
    uint glassObservations = 0u;
    uint opaqueWork = 0u;
    uint glassWork = 0u;
    NwbReflectionFeedbackTile opaqueTile = (NwbReflectionFeedbackTile)0;
    NwbReflectionFeedbackTile glassTile = (NwbReflectionFeedbackTile)0;
    if(pixel.x < g_NwbReflectionParameters.dimensionsAndRoute.x && pixel.y < g_NwbReflectionParameters.dimensionsAndRoute.y){
        opaqueTile = nwbReflectionLoadFeedbackTile(pixel, false);
        glassTile = nwbReflectionLoadFeedbackTile(pixel, true);
        float4 opaqueResult;
        float4 glassResult;
        opaqueCandidate = nwbReflectionClassifySurface(pixel, false, opaqueTile, opaqueResult, opaqueObservations, opaqueWork);
        glassCandidate = nwbReflectionClassifySurface(pixel, true, glassTile, glassResult, glassObservations, glassWork);
        // Every surface receives exactly one dispatch-owned write, including invalid surfaces and true zeros.
        nwbReflectionStore(pixel, false, opaqueResult.rgb, opaqueResult.a);
        nwbReflectionStore(pixel, true, glassResult.rgb, glassResult.a);
    }

    if(sharedWork){
        // Main owns the entire synchronization network. Tail lanes participate even on diagnostic-only Screen or
        // Disabled routes. A zero hardware budget still counts candidates; diagnostics never determine routing.
        GroupMemoryBarrierWithGroupSync();
        const uint maskIndex = groupIndex >> 5u;
        const uint bit = 1u << (groupIndex & 31u);
        if(opaqueCandidate)
            InterlockedOr(g_NwbReflectionCandidateMasks[maskIndex], bit);
        if(glassCandidate)
            InterlockedOr(g_NwbReflectionCandidateMasks[maskIndex + 2u], bit);
        if(observe){
            if(opaqueObservations != 0u)
                InterlockedAdd(g_NwbReflectionObservations[0], opaqueObservations);
            if(glassObservations != 0u)
                InterlockedAdd(g_NwbReflectionObservations[1], glassObservations);
        }
        const uint localWork = opaqueWork + glassWork;
        if(diagnostics && localWork != 0u)
            InterlockedAdd(g_NwbReflectionScreenWork, localWork);
        GroupMemoryBarrierWithGroupSync();
        if(groupIndex == 0u){
            if(hardware){
                const uint candidates = countbits(g_NwbReflectionCandidateMasks[0]) + countbits(g_NwbReflectionCandidateMasks[1])
                    + countbits(g_NwbReflectionCandidateMasks[2]) + countbits(g_NwbReflectionCandidateMasks[3]);
                g_NwbReflectionCandidateBase = 0u;
                if(candidates != 0u){
                    uint firstCandidate;
                    g_NwbReflectionCounters.InterlockedAdd(NWB_REFLECTION_COUNTER_CANDIDATES, candidates, firstCandidate);
                    g_NwbReflectionCandidateBase = firstCandidate;
                }
            }
            if(observe){
                const uint opaque = g_NwbReflectionObservations[0];
                const uint glass = g_NwbReflectionObservations[1];
                const uint potential = (opaque & 127u) + (glass & 127u);
                if(potential != 0u){
                    uint prior;
                    g_NwbReflectionCounters.InterlockedAdd(NWB_REFLECTION_COUNTER_POTENTIAL_RECEIVERS, potential, prior);
                }
                nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_SCREEN_RETURNS, (opaque >> 21u) + (glass >> 21u));
                if(feedbackWrites){
                    const uint tilesX = (g_NwbReflectionParameters.dimensionsAndRoute.x + NWB_REFLECTION_FEEDBACK_GROUP_SIZE - 1u)
                        / NWB_REFLECTION_FEEDBACK_GROUP_SIZE;
                    const uint tileIndex = (pixel.y / NWB_REFLECTION_FEEDBACK_GROUP_SIZE) * tilesX
                        + pixel.x / NWB_REFLECTION_FEEDBACK_GROUP_SIZE;
                    const uint byteOffset = NWB_REFLECTION_FEEDBACK_HEADER_BYTES
                        + tileIndex * NWB_REFLECTION_FEEDBACK_SURFACE_COUNT * NWB_REFLECTION_FEEDBACK_ENTRY_BYTES;
                    g_NwbHeapStorageBuffers[g_NwbReflectionParameters.feedbackWriteSlot].Store2(byteOffset, uint2(
                        nwbReflectionNextFeedbackState(opaqueTile, opaque), nwbReflectionNextFeedbackState(glassTile, glass)
                    ));
                    const uint probes = (opaqueTile.probe && ((opaque >> 14u) & 127u) != 0u ? 1u : 0u)
                        + (glassTile.probe && ((glass >> 14u) & 127u) != 0u ? 1u : 0u);
                    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_FEEDBACK_PROBE_TILES, probes);
                }
            }
            if(diagnostics){
                const uint work = g_NwbReflectionScreenWork;
                const uint iterations = work & 0xffffu;
                if(iterations != 0u){
                    uint prior;
                    g_NwbReflectionCounters.InterlockedAdd(NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_LOW, iterations, prior);
                    if(prior + iterations < prior)
                        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_HIGH, 1u);
                }
                nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_SCREEN_LIMIT_MISSES, (work >> 16u) & 255u);
                nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_FEEDBACK_BYPASSED_PIXELS, work >> 24u);
            }
        }
        GroupMemoryBarrierWithGroupSync();
        if(hardware){
            const uint precedingBits = bit - 1u;
            const uint budget = min(g_NwbReflectionParameters.maxHardwareRays, g_NwbReflectionParameters.queueCapacity);
            if(opaqueCandidate){
                const uint prefix = countbits(g_NwbReflectionCandidateMasks[maskIndex] & precedingBits)
                    + (maskIndex == 0u ? 0u : countbits(g_NwbReflectionCandidateMasks[0]));
                const uint queueIndex = g_NwbReflectionCandidateBase + prefix;
                if(queueIndex < budget)
                    g_NwbReflectionQueue.Store(queueIndex * 4u, pixel.y * g_NwbReflectionParameters.dimensionsAndRoute.x + pixel.x);
            }
            if(glassCandidate){
                const uint prefix = countbits(g_NwbReflectionCandidateMasks[0]) + countbits(g_NwbReflectionCandidateMasks[1])
                    + countbits(g_NwbReflectionCandidateMasks[maskIndex + 2u] & precedingBits)
                    + (maskIndex == 0u ? 0u : countbits(g_NwbReflectionCandidateMasks[2]));
                const uint queueIndex = g_NwbReflectionCandidateBase + prefix;
                if(queueIndex < budget){
                    const uint packedPixel = pixel.y * g_NwbReflectionParameters.dimensionsAndRoute.x + pixel.x
                        + g_NwbReflectionParameters.dimensionsAndRoute.x * g_NwbReflectionParameters.dimensionsAndRoute.y;
                    g_NwbReflectionQueue.Store(queueIndex * 4u, packedPixel);
                }
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

