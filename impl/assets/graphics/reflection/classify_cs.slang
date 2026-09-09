// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.slangi"
#include "screen_trace.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_REFLECTION_CLASSIFY_GROUP_SIZE * NWB_REFLECTION_CLASSIFY_GROUP_SIZE != 64
#error "Reflection candidate compaction requires a 64-lane workgroup"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Two masks per surface cover the 64 classify lanes. Group-shared integer atomics and countbits do not require
// subgroup ballot support; that optional subgroup operation is outside the renderer's device capability floor.
groupshared uint g_NwbReflectionCandidateMasks[4];
groupshared uint g_NwbReflectionCandidateBase;

bool nwbReflectionClassifySurface(const uint2 pixel, const bool glass){
    NwbReflectionSurface surface;
    if(g_NwbReflectionParameters.traceMode == NWB_REFLECTION_MODE_DISABLED || !nwbReflectionLoadSurface(pixel, glass, surface)){
        nwbReflectionStore(pixel, glass, float3(0.0), 0.0);
        return false;
    }
    nwbReflectionRecordDiagnostic(
        glass ? NWB_REFLECTION_COUNTER_GLASS_PIXELS : NWB_REFLECTION_COUNTER_OPAQUE_PIXELS, 1u
    );
    const float3 environment = nwbReflectionEnvironment(surface.direction);
    nwbReflectionStore(pixel, glass, surface.weight * environment, 1.0);
    const bool traceEligible = surface.roughness <= g_NwbReflectionParameters.roughnessCutoff
        && surface.bias < g_NwbReflectionParameters.maxRayDistance;
    const bool screen = g_NwbReflectionParameters.traceMode == NWB_REFLECTION_MODE_SCREEN
        || g_NwbReflectionParameters.traceMode == NWB_REFLECTION_MODE_HYBRID;
    if(screen && traceEligible){
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_SCREEN_ATTEMPTS, 1u);
        float3 radiance;
        float confidence;
        if(nwbReflectionTraceScreen(pixel, surface, radiance, confidence)){
            // The alpha lane retains both route and confidence without a separate full-resolution texture.
            // The hardware continuation replaces this provisional radiance when confidence is insufficient.
            nwbReflectionStore(pixel, glass, surface.weight * lerp(environment, radiance, confidence), 2.0 + 0.5 * confidence);
            if(confidence >= g_NwbReflectionParameters.screenConfidenceThreshold){
                nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_SCREEN_HITS, 1u);
                return false;
            }
        }
    }
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_FALLBACK_PIXELS, 1u);
    const bool hardware = g_NwbReflectionParameters.hardwareEnabled != 0u
        && (g_NwbReflectionParameters.traceMode == NWB_REFLECTION_MODE_HARDWARE
            || g_NwbReflectionParameters.traceMode == NWB_REFLECTION_MODE_HYBRID);
    return hardware && traceEligible;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFLECTION_CLASSIFY_GROUP_SIZE, NWB_REFLECTION_CLASSIFY_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex){
    const uint2 pixel = dispatchThreadId.xy;
    const bool hardware = g_NwbReflectionParameters.hardwareEnabled != 0u
        && (g_NwbReflectionParameters.traceMode == NWB_REFLECTION_MODE_HARDWARE
            || g_NwbReflectionParameters.traceMode == NWB_REFLECTION_MODE_HYBRID);
    if(hardware && groupIndex < 4u)
        g_NwbReflectionCandidateMasks[groupIndex] = 0u;
    bool opaqueCandidate = false;
    bool glassCandidate = false;
    if(pixel.x < g_NwbReflectionParameters.width && pixel.y < g_NwbReflectionParameters.height){
        opaqueCandidate = nwbReflectionClassifySurface(pixel, false);
        glassCandidate = nwbReflectionClassifySurface(pixel, true);
    }

    // This frame-uniform guard skips shared work on screen-only routes. A zero budget still counts candidates.
    if(hardware){
        // Keep the synchronization network in main: every lane, including the partial image tail, participates.
        // Classification may return within its barrier-free helper, but no invocation exits this workgroup early.
        GroupMemoryBarrierWithGroupSync();
        const uint maskIndex = groupIndex >> 5u;
        const uint bit = 1u << (groupIndex & 31u);
        if(opaqueCandidate)
            InterlockedOr(g_NwbReflectionCandidateMasks[maskIndex], bit);
        if(glassCandidate)
            InterlockedOr(g_NwbReflectionCandidateMasks[maskIndex + 2u], bit);
        GroupMemoryBarrierWithGroupSync();
        if(groupIndex == 0u){
            const uint candidates = countbits(g_NwbReflectionCandidateMasks[0]) + countbits(g_NwbReflectionCandidateMasks[1])
                + countbits(g_NwbReflectionCandidateMasks[2]) + countbits(g_NwbReflectionCandidateMasks[3]);
            g_NwbReflectionCandidateBase = 0u;
            // Reserve once for both surfaces, including when the configured ray budget is zero. The global count
            // describes actual candidates; only queue storage and indirect execution are capped by the ray budget.
            if(candidates != 0u){
                uint firstCandidate;
                g_NwbReflectionCounters.InterlockedAdd(NWB_REFLECTION_COUNTER_CANDIDATES, candidates, firstCandidate);
                g_NwbReflectionCandidateBase = firstCandidate;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        const uint precedingBits = bit - 1u;
        const uint budget = min(g_NwbReflectionParameters.maxHardwareRays, g_NwbReflectionParameters.queueCapacity);
        if(opaqueCandidate){
            const uint prefix = countbits(g_NwbReflectionCandidateMasks[maskIndex] & precedingBits)
                + (maskIndex == 0u ? 0u : countbits(g_NwbReflectionCandidateMasks[0]));
            const uint queueIndex = g_NwbReflectionCandidateBase + prefix;
            if(queueIndex < budget)
                g_NwbReflectionQueue.Store(queueIndex * 4u, pixel.y * g_NwbReflectionParameters.width + pixel.x);
        }
        if(glassCandidate){
            const uint prefix = countbits(g_NwbReflectionCandidateMasks[0]) + countbits(g_NwbReflectionCandidateMasks[1])
                + countbits(g_NwbReflectionCandidateMasks[maskIndex + 2u] & precedingBits)
                + (maskIndex == 0u ? 0u : countbits(g_NwbReflectionCandidateMasks[2]));
            const uint queueIndex = g_NwbReflectionCandidateBase + prefix;
            if(queueIndex < budget){
                const uint packedPixel = pixel.y * g_NwbReflectionParameters.width + pixel.x
                    + g_NwbReflectionParameters.width * g_NwbReflectionParameters.height;
                g_NwbReflectionQueue.Store(queueIndex * 4u, packedPixel);
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

