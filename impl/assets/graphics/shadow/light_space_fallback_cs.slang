// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_resources.slangi"
#include "shadow_heap_resources.slangi"
#include "sw_binding_slots.h"

#if !defined(NWB_LIGHT_SPACE_OCCLUDER) || (NWB_LIGHT_SPACE_OCCLUDER != 0 && NWB_LIGHT_SPACE_OCCLUDER != 1)
#error "light-space fallback requires the opaque or transparent compile-time class"
#endif
#define NWB_SW_SHADOW_OCCLUDER NWB_LIGHT_SPACE_OCCLUDER
#define g_NwbSwShadowPush g_NwbLightSpacePush
#define g_NwbSwShadowMaterialContextSlots NwbHeapShadowMaterialContextSlots(g_NwbLightSpacePush.materialContextSlot)
#define NWB_LIGHT_SPACE_DEFERRED NwbHeapDeferredBindlessResources(g_NwbLightSpacePush.deferredResourcesSlot)
#define NWB_SCENE_SHADING_HEAP_SLOT NWB_LIGHT_SPACE_DEFERRED.avboitSlots.z
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT NWB_LIGHT_SPACE_DEFERRED.avboitSlots.w
#include "../scene/buffer.slangi"
#include "sw_shadow_ray.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The map pass initializes every admitted light/pixel. Any unavailable sample requests this complete SW estimate.
[numthreads(NWB_LIGHT_SPACE_GROUP_SIZE, NWB_LIGHT_SPACE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    const uint2 halfExtent = (uint2(g_NwbLightSpacePush.width, g_NwbLightSpacePush.height) + 1u) / 2u;
    if(any(pixel >= halfExtent))
        return;
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint index = 0u; index < lightCount; ++index){
        const NwbSceneLight light = g_NwbSceneLights[index];
        if(light.params.z < 0.0)
            continue;
        const uint3 outputPixel = uint3(pixel, uint(light.params.z));
        if(NwbHeapStorageImage2DArrayFloat4(g_NwbLightSpacePush.outputSlot)[outputPixel].a != 0.0)
            continue;
        const int3 texel = int3(min(pixel * 2u, uint2(g_NwbLightSpacePush.width, g_NwbLightSpacePush.height) - 1u), 0);
        const float3 position = NwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.z).Load(texel).xyz;
        const float3 normal = normalize(NwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.y).Load(texel).xyz * 2.0 - 1.0);
        const uint sampleCount = clamp(g_NwbLightSpacePush.sampleCount, 1u, NWB_SW_SHADOW_TRANSPARENT_SPP);
        // Visibility visibility is a (0, 1) bounded factor; nwbSwShadowLightVisible already returns half3.
        half3 result = half3(0.0h);
        for(uint sample = 0u; sample < sampleCount; ++sample){
#if NWB_LIGHT_SPACE_OCCLUDER == NWB_SW_SHADOW_OCCLUDER_TRANSPARENT
            const uint salt = NWB_SW_SHADOW_TRANSPARENT_JITTER_SALT;
#else
            const uint salt = 0u;
#endif
            const float2 jitter = nwbShadowLowDiscrepancy2D(pixel,
                g_NwbLightSpacePush.frameIndex * NWB_SW_SHADOW_TRANSPARENT_SPP + sample + salt);
            result += nwbSwShadowLightVisible(position, normal, light, jitter) / half(sampleCount);
        }
        NwbHeapStorageImage2DArrayFloat4(g_NwbLightSpacePush.outputSlot)[outputPixel] = float4(result, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

