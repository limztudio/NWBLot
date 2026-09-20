// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Software-only adaptive trace: converged static receivers use one independent sample; all other pixels retain three.

#if !defined(NWB_SW_TRANSPARENT_ADAPTIVE) || (NWB_SW_TRANSPARENT_ADAPTIVE != 0 && NWB_SW_TRANSPARENT_ADAPTIVE != 1)
#error "software transparent sampling requires an explicit fixed or adaptive variant"
#endif

#define NWB_SW_SHADOW_PUSH_DECLARED 1
struct NwbSoftwareTransparentSamplingPush{
    uint width;
    uint height;
    uint instanceCount;
    uint frameIndex;
    uint deferredResourcesHeapSlot;
    uint materialContextSlotsHeapSlot;
    uint transparentSoftHalfStorageSlot;
    uint softSampleCount;
    uint historyValid;
    uint geometryCurrSlot;
    uint geometryPrevSlot;
    uint momentsInSlot;
    row_major float4x4 prevWorldToClip;
};
[[vk::push_constant]] ConstantBuffer<NwbSoftwareTransparentSamplingPush> g_NwbSwShadowPush;

#include "sw_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// TRANSPARENT occluder class: the soft transparent trace skips opaque blockers and integrates the colored crossings only.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_TRANSPARENT

#include "sw_shadow_gbuffer.slangi"

// sw_shadow_ray.slangi pulls shadow_sampling.slangi (nwbShadowLowDiscrepancy2D + the cone jitter) transitively.
#include "sw_shadow_ray.slangi"
#if NWB_SW_TRANSPARENT_ADAPTIVE
#include "software_transparent_sampling.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbSwTransparentSampleCount(const uint2 pixel, const uint lightSlot, const uint maximumSamples, const float3 world){
    if(g_NwbSwShadowPush.historyValid == 0u)
        return maximumSamples;
    const uint2 extent = uint2(g_NwbSwShadowPush.width, g_NwbSwShadowPush.height);
    int2 previousPixel;
    if(!nwbSwTransparentSamplingReproject(pixel, extent, world,
        g_NwbSwShadowPush.prevWorldToClip, g_NwbSwShadowPush.historyValid != 0u, previousPixel))
        return maximumSamples;
    const half4 current = half4(NwbHeapSampledImage2D(g_NwbSwShadowPush.geometryCurrSlot).Load(int3(pixel, 0)));
    const half4 previous = half4(NwbHeapSampledImage2D(g_NwbSwShadowPush.geometryPrevSlot).Load(int3(previousPixel, 0)));
    if(!nwbSwTransparentSamplingGeometryMatches(current, previous))
        return maximumSamples;
    const half3 moments = half3(NwbHeapSampledImage2DArray(g_NwbSwShadowPush.momentsInSlot).Load(int4(previousPixel, lightSlot, 0)).xyz);
    return nwbSwTransparentSamplingBudget(true, moments, maximumSamples);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The half-res soft TRANSPARENT transmittance buffer (one layer per shadow slot) this pass writes the cone-jittered
// colored sample into; the separate RGB shadow_resolve pipeline denoises + fold-multiplies it onto the full-res
// visibility (which already holds the soft OPAQUE result).
#define g_NwbSwShadowTransparentSoftHalf NwbHeapStorageImage2DArrayFloat4(g_NwbSwShadowPush.transparentSoftHalfStorageSlot)

#define NWB_SW_SHADOW_SOFT_TRACE_OUTPUT g_NwbSwShadowTransparentSoftHalf
#define NWB_SW_SHADOW_SOFT_TRACE_SPP NWB_SW_SHADOW_TRANSPARENT_SPP
#define NWB_SW_SHADOW_SOFT_TRACE_JITTER_SALT NWB_SW_SHADOW_TRANSPARENT_JITTER_SALT
#if NWB_SW_TRANSPARENT_ADAPTIVE
#define NWB_SW_SHADOW_SOFT_TRACE_SAMPLE_COUNT(pixel, slot, maximum, world) nwbSwTransparentSampleCount(pixel, slot, maximum, world)
#endif
#include "sw_shadow_soft_trace.slangi"
#if NWB_SW_TRANSPARENT_ADAPTIVE
#undef NWB_SW_SHADOW_SOFT_TRACE_SAMPLE_COUNT
#endif
#undef NWB_SW_SHADOW_SOFT_TRACE_JITTER_SALT
#undef NWB_SW_SHADOW_SOFT_TRACE_SPP
#undef NWB_SW_SHADOW_SOFT_TRACE_OUTPUT


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    nwbSwShadowTraceSoft(dispatchThreadID);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

