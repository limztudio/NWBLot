// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stage-2 adaptive transparent resolve: interpolate where the coarse neighbourhood agrees, re-trace where it
// disagrees, fold onto the full-res opaque mask. Fallback when edge compaction is off.


#include "sw_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// TRANSPARENT occluder class: the edge re-trace casts colored transmittance.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_TRANSPARENT

#include "sw_shadow_gbuffer.slangi"

// The coarse concern declares the COARSE input (read here) and pulls the tracing engine for the edge re-trace.
#include "sw_shadow_coarse.slangi"

// The full-res visibility array: read the opaque mask, fold the transmittance onto it, write back.
#define g_NwbSwShadowVisibility NwbHeapStorageImage2DArrayFloat4(g_NwbSwShadowPush.visibilityStorageSlot)

#include "sw_shadow_coarse_samples.slangi"


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

    const NwbSwShadowCoarseLocation coarseLocation = nwbSwShadowLocateCoarseSample(
        pixel,
        uint2(g_NwbSwShadowPush.coarseWidth, g_NwbSwShadowPush.coarseHeight)
    );
    const half edgeThreshold = half(g_NwbSwShadowPush.edgeThreshold);

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        NwbSwShadowCoarseLight coarseLight;
        if(!nwbSwShadowLoadCoarseLight(coarseLocation, i, edgeThreshold, coarseLight))
            continue;
        const uint slot = coarseLight.slot;

        half3 transmittance;
        if(coarseLight.isEdge){
            // Edge block: re-trace this exact full-res pixel.
            transmittance = nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i], float2(0.0, 0.0));
        }
        else{
            // Flat interior: bilinearly interpolate the coarse transmittance.
            transmittance = nwbSwShadowInterpolateCoarse(coarseLight.samples, coarseLocation.weights);
        }

        const half3 opaqueMask = half3(g_NwbSwShadowVisibility[uint3(pixel, slot)].rgb);
        g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(opaqueMask * transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

