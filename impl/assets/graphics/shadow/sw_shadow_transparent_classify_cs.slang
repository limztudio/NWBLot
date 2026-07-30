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

#include "sw_shadow_gbuffer.slangi"
#include "sw_shadow_edge_record.slangi"

// The classify pass reads/writes all worksets through their storage heap slots.
#define g_NwbSwShadowCoarse NwbHeapStorageImage2DArrayFloat4(g_NwbSwShadowPush.coarseStorageSlot)
#define g_NwbSwShadowVisibility NwbHeapStorageImage2DArrayFloat4(g_NwbSwShadowPush.visibilityStorageSlot)
#define g_NwbSwShadowEdgeStats NwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.edgeStatsStorageSlot)
#define g_NwbSwShadowEdgeCounter NwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.edgeCounterStorageSlot)
#define g_NwbSwShadowEdgeList NwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.edgeListStorageSlot)

#include "sw_shadow_coarse_samples.slangi"


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

    const NwbSwShadowCoarseLocation coarseLocation = nwbSwShadowLocateCoarseSample(
        pixel,
        uint2(g_NwbSwShadowPush.coarseWidth, g_NwbSwShadowPush.coarseHeight)
    );
    const float edgeThreshold = g_NwbSwShadowPush.edgeThreshold;
    const uint edgeCapacity = g_NwbSwShadowPush.edgeCapacity;

    uint tracedRays = 0u;
    uint totalRays = 0u;

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        NwbSwShadowCoarseLight coarseLight;
        if(!nwbSwShadowLoadCoarseLight(coarseLocation, i, edgeThreshold, coarseLight))
            continue;
        const uint slot = coarseLight.slot;
        ++totalRays;

        if(coarseLight.isEdge){
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
                const half3 interp = nwbSwShadowInterpolateCoarse(coarseLight.samples, coarseLocation.weights);
                const half3 opaqueMask = half3(g_NwbSwShadowVisibility[uint3(pixel, slot)].rgb);
                g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(opaqueMask * interp, 1.0);
            }
        }
        else{
            // FLAT interior: interpolate the coarse transmittance and fold onto the pristine opaque mask in place.
            const half3 interp = nwbSwShadowInterpolateCoarse(coarseLight.samples, coarseLocation.weights);
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

