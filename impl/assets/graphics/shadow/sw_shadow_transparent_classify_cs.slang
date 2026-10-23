// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stage-3 transparent shadow classify pass: appends edge texels for indirect full-resolution trace and interpolates smooth tiles.


#include "sw_binding_slots.h"

#include "sw_shadow_gbuffer.slangi"
#include "sw_shadow_edge_record.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The classify pass reads/writes all worksets through their storage heap slots.
#define g_NwbSwShadowCoarse nwbHeapStorageImage2DArrayFloat4(g_NwbSwShadowPush.coarseStorageSlot)
#define g_NwbSwShadowVisibility nwbHeapStorageImage2DArrayFloat4(g_NwbSwShadowPush.visibilityStorageSlot)
#define g_NwbSwShadowEdgeCounter nwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.edgeCounterStorageSlot)
#define g_NwbSwShadowEdgeList nwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.edgeListStorageSlot)

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

    const NwbSwShadowCoarseLocation coarseLocation = nwbSwShadowLocateCoarseSample(pixel, uint2(g_NwbSwShadowPush.coarseWidth, g_NwbSwShadowPush.coarseHeight));
    const half edgeThreshold = half(g_NwbSwShadowPush.edgeThreshold);
    const uint edgeCapacity = g_NwbSwShadowPush.edgeCapacity;

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        NwbSwShadowCoarseLight coarseLight;
        if(!nwbSwShadowLoadCoarseLight(coarseLocation, i, edgeThreshold, coarseLight))
            continue;
        const uint slot = coarseLight.slot;

        if(coarseLight.isEdge){
            // Edge texel: append (pixel, lightIndex) for indirect trace while leaving the opaque mask intact; fall back to interpolation on overflow.
            uint appendIndex;
            InterlockedAdd(g_NwbSwShadowEdgeCounter[NWB_SW_SHADOW_EDGE_COUNTER_APPEND], 1u, appendIndex);
            if(appendIndex < edgeCapacity){
                const uint base = appendIndex * NWB_SW_SHADOW_EDGE_RECORD_WORDS;
                g_NwbSwShadowEdgeList[base] = nwbSwShadowPackPixel(pixel);
                g_NwbSwShadowEdgeList[base + 1u] = i;
            }
            else{
                // OVERFLOW: this edge will not be traced, so degrade it to the interpolated fallback rather than leaving the un-tinted opaque mask. Near-impossible at the allocated capacity.
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
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

