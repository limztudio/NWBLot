// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Half-resolution soft opaque trace: averages NWB_SW_SHADOW_SOFT_SPP cone-jittered rays per shadow slot for shadow_resolve.


#include "sw_binding_slots.h"

// OPAQUE occluder class: the soft trace is opaque-only.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_OPAQUE

#include "sw_shadow_gbuffer.slangi"

// sw_shadow_ray.slangi pulls shadow_sampling.slangi (nwbShadowLowDiscrepancy2D + the cone jitter) transitively.
#include "sw_shadow_ray.slangi"

// Half-resolution soft opaque visibility output (one layer per shadow slot) consumed by shadow_resolve.
#define g_NwbSwShadowSoftHalf nwbHeapStorageImage2DArrayFloat4(g_NwbSwShadowPush.softHalfStorageSlot)

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

