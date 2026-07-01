// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Adaptive-opaque COARSE trace (downscaled). One thread per coarse texel: trace one OPAQUE-only any-hit per
// NWB_SW_SHADOW_COARSE_FACTOR block into the coarse buffer (binary 0/1, since the occluder filter is opaque-only). The
// cheap base layer the opaque adaptive resolve interpolates over the flat interior and re-traces at silhouette edges;
// replaces the front of the full-res opaque pre-pass on the adaptive-opaque path (NWB_SW_SHADOW_ADAPTIVE_OPAQUE=1).


#include "sw_binding_slots.h"

// OPAQUE occluder class: the coarse transmittance is a BINARY blocker value.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_OPAQUE

// This pass declares its OWN push struct + composes only the coarse trace concern. The coarse concern declares the
// COARSE output and pulls the gbuffer + tracing engine.
struct NwbSwShadowOpaqueCoarsePushConstants{
    uint width;
    uint height;
    uint instanceCount;
    uint coarseWidth;
    uint coarseHeight;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowOpaqueCoarsePushConstants> g_NwbSwShadowPush;

#include "sw_shadow_coarse.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    nwbSwShadowCoarseTrace(dispatchThreadID.xy);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
