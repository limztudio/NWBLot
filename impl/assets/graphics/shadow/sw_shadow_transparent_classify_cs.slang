// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sw_binding_slots.h"

#include "sw_shadow_gbuffer.slangi"
#include "sw_shadow_edge_record.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

        bool enqueued = false;
        if(coarseLight.isEdge){
            uint appendIndex;
            InterlockedAdd(g_NwbSwShadowEdgeCounter[NWB_SW_SHADOW_EDGE_COUNTER_APPEND], 1u, appendIndex);
            if(appendIndex < edgeCapacity){
                const uint base = appendIndex * NWB_SW_SHADOW_EDGE_RECORD_WORDS;
                g_NwbSwShadowEdgeList[base] = nwbSwShadowPackPixel(pixel);
                g_NwbSwShadowEdgeList[base + 1u] = i;
                enqueued = true;
            }
        }
        if(!enqueued){
            const half3 interp = nwbSwShadowInterpolateCoarse(coarseLight.samples, coarseLocation.weights);
            const half3 opaqueMask = half3(g_NwbSwShadowVisibility[uint3(pixel, slot)].rgb);
            g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(opaqueMask * interp, 1.0);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

