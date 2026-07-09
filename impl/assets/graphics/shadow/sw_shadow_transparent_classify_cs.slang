// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stage-3 CLASSIFY + FILL (compacted transparent resolve). Byte-identical edge test + interpolation to the Stage-2
// adaptive resolve, but EDGE slots are NOT traced here: they are atomically APPENDED to the compacted edge list and the
// edge texel is left holding the PRISTINE opaque mask, so the indirect trace pass can read that mask and fold
// transmittance with a SINGLE overwrite -- never double-multiplying the opaque pass's read-modify-write fold. On list
// overflow (index >= edgeCapacity) the edge texel degrades to the interpolated fallback instead of being left un-tinted.
// This pass does NOT trace (no occlusion ray), so it needs neither the traversal engine nor an occluder class -- only
// the coarse neighbourhood, the visibility mask, and the compaction buffers.


#include "sw_binding_slots.h"

// The scene light list this classify loop iterates. Redirect its bindings onto this pass's set before scene/buffer.slangi.
#define NWB_SCENE_SHADING_SET NWB_SW_SHADOW_SET
#define NWB_SCENE_SHADING_BINDING NWB_SW_SHADOW_BINDING_SCENE_SHADING
#define NWB_SCENE_LIGHT_LIST_SET NWB_SW_SHADOW_SET
#define NWB_SCENE_LIGHT_LIST_BINDING NWB_SW_SHADOW_BINDING_LIGHT_LIST

#include "sw_shadow_gbuffer.slangi"
#include "sw_shadow_edge_record.slangi"
#include "../scene/buffer.slangi"

struct NwbSwShadowTransparentClassifyPushConstants{
    uint width;
    uint height;
    uint coarseWidth;
    uint coarseHeight;
    // 2x2 coarse transmittance range above which a block is an EDGE (appended) rather than FLAT (interpolated in place).
    float edgeThreshold;
    // When non-zero, atomically tally traced-vs-total (edge-vs-total) rays into the edge-stats buffer (measurement only).
    uint collectStats;
    // Element capacity of the edge-list buffer in RECORDS; the appended index is guarded against this (overflow degrades
    // to the interpolated fallback).
    uint edgeCapacity;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowTransparentClassifyPushConstants> g_NwbSwShadowPush;

// The coarse (downscaled) transmittance neighbourhood read for the edge test / flat interpolation.
[[vk::binding(NWB_SW_SHADOW_BINDING_COARSE, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowCoarse;

// The full-res visibility array: flat interior folds transmittance in place; edge texels are left pristine for the
// indirect trace pass (overflow folds the interpolated fallback in place).
[[vk::binding(NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowVisibility;

// Edge-fraction instrumentation counter ([0] traced/edge rays, [1] total candidate rays).
[[vk::binding(NWB_SW_SHADOW_BINDING_EDGE_STATS, NWB_SW_SHADOW_SET)]]
RWStructuredBuffer<uint> g_NwbSwShadowEdgeStats;

// Stage-3 compaction: the edge-pixel append counter ([0] atomic append count) + the compacted edge-record list
// (NWB_SW_SHADOW_EDGE_RECORD_WORDS u32 per record) this pass appends into.
[[vk::binding(NWB_SW_SHADOW_BINDING_EDGE_COUNTER, NWB_SW_SHADOW_SET)]]
RWStructuredBuffer<uint> g_NwbSwShadowEdgeCounter;

[[vk::binding(NWB_SW_SHADOW_BINDING_EDGE_LIST, NWB_SW_SHADOW_SET)]]
RWStructuredBuffer<uint> g_NwbSwShadowEdgeList;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbSwShadowPush.width || pixel.y >= g_NwbSwShadowPush.height)
        return;
    const int3 texel = int3(int2(pixel), 0);

    // Background: leave the pristine (white) opaque mask; it casts no candidate rays.
    if(nwbSwShadowIsBackground(texel))
        return;

    const float2 coarseCoord = float2(pixel) * (1.0 / float(NWB_SW_SHADOW_COARSE_FACTOR));
    const uint2 c0 = uint2(floor(coarseCoord));
    const uint2 c1 = min(c0 + uint2(1u, 1u), uint2(g_NwbSwShadowPush.coarseWidth - 1u, g_NwbSwShadowPush.coarseHeight - 1u));
    const float2 w = coarseCoord - float2(c0);

    const float edgeThreshold = g_NwbSwShadowPush.edgeThreshold;
    const uint edgeCapacity = g_NwbSwShadowPush.edgeCapacity;

    uint tracedRays = 0u;
    uint totalRays = 0u;

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slotF = g_NwbSceneLights[i].params.z;
        if(slotF < 0.0)
            continue;
        const uint slot = uint(slotF);
        ++totalRays;

        const half3 t00 = half3(g_NwbSwShadowCoarse[uint3(c0.x, c0.y, slot)].rgb);
        const half3 t10 = half3(g_NwbSwShadowCoarse[uint3(c1.x, c0.y, slot)].rgb);
        const half3 t01 = half3(g_NwbSwShadowCoarse[uint3(c0.x, c1.y, slot)].rgb);
        const half3 t11 = half3(g_NwbSwShadowCoarse[uint3(c1.x, c1.y, slot)].rgb);

        const half3 lo = min(min(t00, t10), min(t01, t11));
        const half3 hi = max(max(t00, t10), max(t01, t11));
        const half range = max(max(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z);

        if(float(range) > edgeThreshold){
            // EDGE: append (pixel, lightIndex) to the compacted list; DO NOT trace and DO NOT write the texel -- leave
            // the pristine opaque mask for the indirect trace's single overwrite. Always increment the counter (so the
            // true edge total is measured) but guard the indexed write to the allocated capacity.
            ++tracedRays;
            uint appendIndex;
            InterlockedAdd(g_NwbSwShadowEdgeCounter[NWB_SW_SHADOW_EDGE_COUNTER_APPEND], 1u, appendIndex);
            if(appendIndex < edgeCapacity){
                const uint base = appendIndex * NWB_SW_SHADOW_EDGE_RECORD_WORDS;
                g_NwbSwShadowEdgeList[base] = nwbSwShadowPackPixel(pixel);
                g_NwbSwShadowEdgeList[base + 1u] = i;
            }
            else{
                // OVERFLOW: this edge will not be traced, so degrade it to the interpolated fallback rather than leaving
                // the un-tinted opaque mask. Near-impossible at the allocated capacity.
                const half3 tx0 = lerp(t00, t10, half(w.x));
                const half3 tx1 = lerp(t01, t11, half(w.x));
                const half3 interp = lerp(tx0, tx1, half(w.y));
                const half3 opaqueMask = half3(g_NwbSwShadowVisibility[uint3(pixel, slot)].rgb);
                g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(opaqueMask * interp, 1.0);
            }
        }
        else{
            // FLAT interior: interpolate the coarse transmittance and fold onto the pristine opaque mask in place.
            const half3 tx0 = lerp(t00, t10, half(w.x));
            const half3 tx1 = lerp(t01, t11, half(w.x));
            const half3 interp = lerp(tx0, tx1, half(w.y));
            const half3 opaqueMask = half3(g_NwbSwShadowVisibility[uint3(pixel, slot)].rgb);
            g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(opaqueMask * interp, 1.0);
        }
    }

    if(g_NwbSwShadowPush.collectStats != 0u && totalRays > 0u){
        InterlockedAdd(g_NwbSwShadowEdgeStats[NWB_SW_SHADOW_EDGE_STATS_TRACED], tracedRays);
        InterlockedAdd(g_NwbSwShadowEdgeStats[NWB_SW_SHADOW_EDGE_STATS_TOTAL], totalRays);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

