// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_BINDLESS_TLAS
#error "NWB_BINDLESS_TLAS is fixed by this hardware shader source"
#endif
#define NWB_BINDLESS_TLAS 1

#include "binding_slots.h"
#include "constants.h"
#include "../scene/binding_slots.h"
#include "shadow_heap_resources.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbShadowRqPushConstants{
    uint frameIndex;
    uint worldPositionSlot;
    uint normalSlot;
    uint depthSlot;
    uint deferredResourcesHeapSlot;
    uint visibilityStorageSlot;
};

[[vk::push_constant]] ConstantBuffer<NwbShadowRqPushConstants> g_NwbShadowRqPushConstants;

#define NWB_SHADOW_RQ_DEFERRED_RESOURCES_HEAP_SLOT g_NwbShadowRqPushConstants.deferredResourcesHeapSlot
#define NWB_SHADOW_RQ_VISIBILITY_STORAGE_SLOT g_NwbShadowRqPushConstants.visibilityStorageSlot
#include "shadow_rayquery.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_RT_GROUP_SIZE, NWB_SHADOW_RT_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    uint width;
    uint height;
    uint slotElems;
    g_NwbShadowVisibility.GetDimensions(width, height, slotElems);

    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= width || pixel.y >= height)
        return;

    const int3 texel = int3(int2(pixel), 0);

    if(nwbHeapSampledImage2D(g_NwbShadowRqPushConstants.depthSlot).Load(texel).r >= NWB_SCENE_BACKGROUND_DEPTH){
        for(uint slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot)
            g_NwbShadowVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const half3 worldPositionSample = half3(nwbHeapSampledImage2D(g_NwbShadowRqPushConstants.worldPositionSlot).Load(texel).xyz);
    const half3 normalSample = half3(nwbHeapSampledImage2D(g_NwbShadowRqPushConstants.normalSlot).Load(texel).xyz);
    const float3 worldPosition = float3(worldPositionSample);
    const float3 normal = normalize(float3(normalSample) * 2.0 - 1.0);

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;

        const float2 jitter = nwbShadowLowDiscrepancy2D(pixel, g_NwbShadowRqPushConstants.frameIndex);
        const half3 transmittance = nwbShadowRqLightVisible(worldPosition, normal, g_NwbSceneLights[i], jitter);
        g_NwbShadowVisibility[uint3(pixel, uint(slot))] = float4(transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

