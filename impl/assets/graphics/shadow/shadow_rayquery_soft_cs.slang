// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_BINDLESS_TLAS
#error "NWB_BINDLESS_TLAS is fixed by this hardware shader source"
#endif
#define NWB_BINDLESS_TLAS 1

// Hardware and software traces share the receiver grid and cone-jittered sample conventions.


#include "binding_slots.h"
#include "shadow_heap_resources.slangi"
#include "sw_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbShadowRqSoftPushConstants{
    uint width;
    uint height;
    uint frameIndex;
    uint worldPositionSlot;
    uint normalSlot;
    uint depthSlot;
    uint deferredResourcesHeapSlot;
    uint visibilityStorageSlot;
    uint softSampleCount;
    uint receiverFactor;
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowRqSoftPushConstants> g_NwbShadowRqSoftPushConstants;

#define NWB_SHADOW_RQ_DEFERRED_RESOURCES_HEAP_SLOT g_NwbShadowRqSoftPushConstants.deferredResourcesHeapSlot
#define NWB_SHADOW_RQ_VISIBILITY_STORAGE_SLOT g_NwbShadowRqSoftPushConstants.visibilityStorageSlot
#include "shadow_rayquery.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_RT_GROUP_SIZE, NWB_SHADOW_RT_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 halfPixel = dispatchThreadID.xy;
    // Sample each receiver block's top-left texel, matching geometry downsampling and software tracing.
    const uint halfWidth = (g_NwbShadowRqSoftPushConstants.width + g_NwbShadowRqSoftPushConstants.receiverFactor - 1u) / g_NwbShadowRqSoftPushConstants.receiverFactor;
    const uint halfHeight = (g_NwbShadowRqSoftPushConstants.height + g_NwbShadowRqSoftPushConstants.receiverFactor - 1u) / g_NwbShadowRqSoftPushConstants.receiverFactor;
    if(halfPixel.x >= halfWidth || halfPixel.y >= halfHeight)
        return;

    const uint2 base = halfPixel * g_NwbShadowRqSoftPushConstants.receiverFactor;
    const int3 texel = int3(int2(min(base.x, g_NwbShadowRqSoftPushConstants.width - 1u), min(base.y, g_NwbShadowRqSoftPushConstants.height - 1u)), 0);
    const uint sampleCount = min(max(g_NwbShadowRqSoftPushConstants.softSampleCount, 1u), NWB_SW_SHADOW_SOFT_SPP);

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);

    // Background: fully LIT (white) for every slot -- the multiplicative identity the resolve carries and the value the
    // upsample writes for a background full-res pixel, so the two agree at the geometry/background edge. Same depth
    // threshold as the SW trace (NWB_SW_SHADOW_BACKGROUND_DEPTH aliases the HW full-res threshold).
    if(nwbHeapSampledImage2D(g_NwbShadowRqSoftPushConstants.depthSlot).Load(texel).r >= NWB_SW_SHADOW_BACKGROUND_DEPTH){
        for(uint i = 0u; i < lightCount; ++i){
            const float slot = g_NwbSceneLights[i].params.z;
            if(slot < 0.0)
                continue;
            g_NwbShadowVisibility[uint3(halfPixel, uint(slot))] = float4(1.0, 1.0, 1.0, 1.0);
        }
        return;
    }

    // The deferred vector attachments are RGBA16F. Keep the half payload through the fetch and expand only for
    // the ray origin/normal calculations, which must remain FP32 for stable soft-shadow tracing.
    const half3 worldPositionSample = half3(nwbHeapSampledImage2D(g_NwbShadowRqSoftPushConstants.worldPositionSlot).Load(texel).xyz);
    const half3 normalSample = half3(nwbHeapSampledImage2D(g_NwbShadowRqSoftPushConstants.normalSlot).Load(texel).xyz);
    const float3 worldPosition = float3(worldPositionSample);
    const float3 normal = normalize(float3(normalSample) * 2.0 - 1.0);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;
        // Opaque-only jittered samples (per pixel/frame/sample decorrelated); averaging the runtime budget yields the
        // 0..1 coverage the value-aware resolve needs. Temporal accumulation carries sampling across frames.
        half3 accum = half3(0.0h, 0.0h, 0.0h);
        [loop] for(uint s = 0u; s < sampleCount; ++s){
            const float2 sampleJitter = nwbShadowLowDiscrepancy2D(halfPixel, g_NwbShadowRqSoftPushConstants.frameIndex * NWB_SW_SHADOW_SOFT_SPP + s);
            accum += nwbShadowRqLightVisible(worldPosition, normal, g_NwbSceneLights[i], sampleJitter);
        }
        const half3 transmittance = accum * half(1.0 / float(sampleCount));
        g_NwbShadowVisibility[uint3(halfPixel, uint(slot))] = float4(transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

