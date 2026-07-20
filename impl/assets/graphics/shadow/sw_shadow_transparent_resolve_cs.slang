// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stage-2 ADAPTIVE transparent resolve (full resolution). One thread per full-res pixel: read the coarse neighbourhood;
// where the colored transmittance AGREES (flat shadow) bilinearly interpolate it (cheap), where it DISAGREES (an edge)
// re-trace THIS pixel at full res (sharp). Either way fold the result onto this pixel's own full-res opaque mask, so the
// opaque (hard) shadow stays full-res while the soft transparent shadow costs one trace per block in the interior and
// per-pixel only along its edges. The A/B baseline for the Stage-3 compacted path (dispatched when NWB_SW_SHADOW_COMPACT=0).


#include "sw_binding_slots.h"

// TRANSPARENT occluder class: the edge re-trace casts colored transmittance.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_TRANSPARENT

#include "sw_shadow_gbuffer.slangi"

struct NwbSwShadowTransparentResolvePushConstants{
    uint width;
    uint height;
    uint instanceCount;
    uint coarseWidth;
    uint coarseHeight;
    // 2x2 coarse transmittance range above which a block is re-traced (else bilinearly interpolated).
    float edgeThreshold;
    // When non-zero, atomically tally traced-vs-total rays into the edge-stats buffer (measurement only).
    uint collectStats;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowTransparentResolvePushConstants> g_NwbSwShadowPush;

// The coarse concern declares the COARSE input (read here) and pulls the tracing engine for the edge re-trace.
#include "sw_shadow_coarse.slangi"

// The full-res visibility array: read the opaque mask, fold the transmittance onto it, write back.
[[vk::binding(NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowVisibility;

#include "sw_shadow_coarse_samples.slangi"

// Edge-fraction instrumentation counter ([0] full-res traced rays, [1] total candidate rays). Tallied only when
// collectStats is set; read back to the log to decide the Stage-3 payoff.
[[vk::binding(NWB_SW_SHADOW_BINDING_EDGE_STATS, NWB_SW_SHADOW_SET)]]
RWStructuredBuffer<uint> g_NwbSwShadowEdgeStats;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbSwShadowPush.width || pixel.y >= g_NwbSwShadowPush.height)
        return;
    const int3 texel = int3(int2(pixel), 0);

    // Background: the opaque pass already left it white; nothing to multiply, and it casts no candidate rays.
    if(nwbSwShadowIsBackground(texel))
        return;

    const float3 worldPosition = nwbSwShadowLoadWorldPosition(texel);
    const float3 normal = nwbSwShadowLoadNormal(texel);

    // Continuous coarse coordinate: coarse sample k sits at full-res k*FACTOR, so a full-res pixel maps to k =
    // pixel/FACTOR. floor + frac give the bracketing coarse texels and the bilinear weights; clamp the upper texel.
    const float2 coarseCoord = float2(pixel) * (1.0 / float(NWB_SW_SHADOW_COARSE_FACTOR));
    const uint2 c0 = uint2(floor(coarseCoord));
    const uint2 c1 = min(c0 + uint2(1u, 1u), uint2(g_NwbSwShadowPush.coarseWidth - 1u, g_NwbSwShadowPush.coarseHeight - 1u));
    const float2 w = coarseCoord - float2(c0);

    const float edgeThreshold = g_NwbSwShadowPush.edgeThreshold;

    uint tracedRays = 0u;
    uint totalRays = 0u;

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slotF = g_NwbSceneLights[i].params.z;
        if(slotF < 0.0)
            continue;
        const uint slot = uint(slotF);
        ++totalRays;

        const NwbSwShadowCoarseSamples samples = nwbSwShadowLoadCoarseSamples(c0, c1, slot);
        const half range = nwbSwShadowCoarseRange(samples);

        half3 transmittance;
        if(float(range) > edgeThreshold){
            // Edge block: re-trace this exact full-res pixel.
            transmittance = nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i], float2(0.0, 0.0));
            ++tracedRays;
        }
        else{
            // Flat interior: bilinearly interpolate the coarse transmittance.
            transmittance = nwbSwShadowInterpolateCoarse(samples, w);
        }

        const half3 opaqueMask = half3(g_NwbSwShadowVisibility[uint3(pixel, slot)].rgb);
        g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(opaqueMask * transmittance, 1.0);
    }

    // Edge-fraction instrumentation: one atomic pair per lit pixel (only when measuring). traced/total over the frame is
    // the fraction of candidate rays that still needed a full-res trace -- the number that decides the Stage-3 payoff.
    if(g_NwbSwShadowPush.collectStats != 0u && totalRays > 0u){
        InterlockedAdd(g_NwbSwShadowEdgeStats[NWB_SW_SHADOW_EDGE_STATS_TRACED], tracedRays);
        InterlockedAdd(g_NwbSwShadowEdgeStats[NWB_SW_SHADOW_EDGE_STATS_TOTAL], totalRays);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

