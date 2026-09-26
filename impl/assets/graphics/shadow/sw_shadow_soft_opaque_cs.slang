// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Soft opaque half-res trace: one thread per half texel averages NWB_SW_SHADOW_SOFT_SPP cone-jittered occlusion rays
// per shadow-slot light into the half-res buffer for the resolve to denoise + upsample. Seeded per pixel/frame so
// neighbours stay decorrelated; light-type handling lives in nwbSwShadowLightVisible.


#include "sw_binding_slots.h"

// OPAQUE occluder class: the soft trace is opaque-only.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_OPAQUE

#include "sw_shadow_gbuffer.slangi"

// sw_shadow_ray.slangi pulls shadow_sampling.slangi (nwbShadowLowDiscrepancy2D + the cone jitter) transitively.
#include "sw_shadow_ray.slangi"

// The half-res soft opaque visibility buffer (one layer per shadow slot) this pass writes the cone-jittered sample
// into; the separate shadow_resolve pipeline denoises + upsamples it into the full-res visibility.
#define g_NwbSwShadowSoftHalf NwbHeapStorageImage2DArrayFloat4(g_NwbSwShadowPush.softHalfStorageSlot)

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

