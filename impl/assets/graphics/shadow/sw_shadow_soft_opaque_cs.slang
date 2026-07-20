// SOFT OPAQUE half-res trace (soft-ray-traced-shadow feature, all light types). One thread per HALF-res pixel: for each
// light holding a shadow slot, cast NWB_SW_SHADOW_SOFT_SPP OPAQUE occlusion rays CONE-JITTERED inside the cone the
// light's physical source subtends and write the averaged single-frame visibility into the half-res soft buffer. A
// DIRECTIONAL light jitters inside its constant angular radius (params2.x); a POINT/SPOT light inside the distance-
// dependent half-angle asin(params2.y/dist) its source sphere (radius params2.y) subtends -- both handled inside
// nwbSwShadowLightVisible, so this pass is light-type-agnostic and just processes every slot light. The separate
// shadow_resolve pipeline then denoises + upsamples the result into the full-res visibility. The jitter is the per-
// pixel/per-frame low-discrepancy sample, seeded by the half-res pixel + frameIndex so each pixel walks the source over
// frames while neighbours stay decorrelated (the denoise then averages independent estimates).


#include "sw_binding_slots.h"

// OPAQUE occluder class: the soft trace is opaque-only.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_OPAQUE

#include "sw_shadow_gbuffer.slangi"

struct NwbSwShadowSoftOpaquePushConstants{
    uint width;
    uint height;
    uint instanceCount;
    // The renderer frame counter, seeding the per-pixel low-discrepancy cone-jitter sample so the jittered rays walk
    // the source across frames (decorrelated per pixel by a hash rotation).
    uint frameIndex;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowSoftOpaquePushConstants> g_NwbSwShadowPush;

// sw_shadow_ray.slangi pulls shadow_sampling.slangi (nwbShadowLowDiscrepancy2D + the cone jitter) transitively.
#include "sw_shadow_ray.slangi"

// The half-res soft opaque visibility buffer (one layer per shadow slot) this pass writes the cone-jittered sample
// into; the separate shadow_resolve pipeline denoises + upsamples it into the full-res visibility.
[[vk::binding(NWB_SW_SHADOW_BINDING_SOFT_HALF, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowSoftHalf;

#define NWB_SW_SHADOW_SOFT_TRACE_OUTPUT g_NwbSwShadowSoftHalf
#define NWB_SW_SHADOW_SOFT_TRACE_SPP NWB_SW_SHADOW_SOFT_SPP
#define NWB_SW_SHADOW_SOFT_TRACE_JITTER_SALT 0u
#include "sw_shadow_soft_trace.slangi"
#undef NWB_SW_SHADOW_SOFT_TRACE_JITTER_SALT
#undef NWB_SW_SHADOW_SOFT_TRACE_SPP
#undef NWB_SW_SHADOW_SOFT_TRACE_OUTPUT


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    nwbSwShadowTraceSoft(dispatchThreadID);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

