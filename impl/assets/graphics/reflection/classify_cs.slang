// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.slangi"
#include "screen_trace.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbReflectionClassifySurface(const uint2 pixel, const bool glass){
    NwbReflectionSurface surface;
    if(g_NwbReflectionParameters.traceMode == NWB_REFLECTION_MODE_DISABLED || !nwbReflectionLoadSurface(pixel, glass, surface)){
        nwbReflectionStore(pixel, glass, float3(0.0), 0.0);
        return;
    }
    uint previousCount;
    g_NwbReflectionCounters.InterlockedAdd(
        glass ? NWB_REFLECTION_COUNTER_GLASS_PIXELS : NWB_REFLECTION_COUNTER_OPAQUE_PIXELS, 1u, previousCount
    );
    const float3 environment = nwbReflectionEnvironment(surface.direction);
    nwbReflectionStore(pixel, glass, surface.weight * environment, 1.0);
    const bool traceEligible = surface.roughness <= g_NwbReflectionParameters.roughnessCutoff
        && surface.bias < g_NwbReflectionParameters.maxRayDistance;
    const bool screen = g_NwbReflectionParameters.traceMode == NWB_REFLECTION_MODE_SCREEN
        || g_NwbReflectionParameters.traceMode == NWB_REFLECTION_MODE_HYBRID;
    if(screen && traceEligible){
        g_NwbReflectionCounters.InterlockedAdd(NWB_REFLECTION_COUNTER_SCREEN_ATTEMPTS, 1u, previousCount);
        float3 radiance;
        float confidence;
        if(nwbReflectionTraceScreen(pixel, surface, radiance, confidence)){
            // The alpha lane retains both route and confidence without a separate full-resolution texture.
            // The hardware continuation replaces this provisional radiance when confidence is insufficient.
            nwbReflectionStore(pixel, glass, surface.weight * lerp(environment, radiance, confidence), 2.0 + 0.5 * confidence);
            if(confidence >= g_NwbReflectionParameters.screenConfidenceThreshold){
                g_NwbReflectionCounters.InterlockedAdd(NWB_REFLECTION_COUNTER_SCREEN_HITS, 1u, previousCount);
                return;
            }
        }
    }
    g_NwbReflectionCounters.InterlockedAdd(NWB_REFLECTION_COUNTER_FALLBACK_PIXELS, 1u, previousCount);
    const bool hardware = g_NwbReflectionParameters.hardwareEnabled != 0u
        && (g_NwbReflectionParameters.traceMode == NWB_REFLECTION_MODE_HARDWARE
            || g_NwbReflectionParameters.traceMode == NWB_REFLECTION_MODE_HYBRID);
    if(!hardware || !traceEligible)
        return;
    uint queueIndex;
    g_NwbReflectionCounters.InterlockedAdd(NWB_REFLECTION_COUNTER_CANDIDATES, 1u, queueIndex);
    const uint budget = min(g_NwbReflectionParameters.maxHardwareRays, g_NwbReflectionParameters.queueCapacity);
    if(queueIndex < budget){
        const uint packedPixel = pixel.y * g_NwbReflectionParameters.width + pixel.x
            + (glass ? g_NwbReflectionParameters.width * g_NwbReflectionParameters.height : 0u);
        g_NwbReflectionQueue.Store(queueIndex * 4u, packedPixel);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFLECTION_CLASSIFY_GROUP_SIZE, NWB_REFLECTION_CLASSIFY_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadId.xy;
    if(pixel.x >= g_NwbReflectionParameters.width || pixel.y >= g_NwbReflectionParameters.height)
        return;
    nwbReflectionClassifySurface(pixel, false);
    nwbReflectionClassifySurface(pixel, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

