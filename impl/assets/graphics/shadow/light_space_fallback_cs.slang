// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_LIGHT_SPACE_CSG_ENABLED
#define NWB_LIGHT_SPACE_CSG_ENABLED 0
#endif
#define NWB_CSG_SHADOW_HW_TRACE 0
#define NWB_CSG_RAY_HARDWARE NWB_CSG_SHADOW_HW_TRACE

#include "light_space_resources.slangi"
#include "shadow_heap_resources.slangi"
#include "sw_binding_slots.h"

#if !defined(NWB_LIGHT_SPACE_OCCLUDER) || (NWB_LIGHT_SPACE_OCCLUDER != 0 && NWB_LIGHT_SPACE_OCCLUDER != 1)
#error "light-space fallback requires the opaque or transparent compile-time class"
#endif
#include "light_space_deferred.slangi"
#define NWB_SW_SHADOW_SKIP_CSG NWB_LIGHT_SPACE_CSG_ENABLED
#include "sw_shadow_ray.slangi"
#if NWB_LIGHT_SPACE_CSG_ENABLED
#include "csg_shadow_fallback.slangi"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_LIGHT_SPACE_FALLBACK_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint itemIndex = dispatchThreadID.x;
    const uint fallbackCount = nwbHeapRawBuffer(g_NwbLightSpacePush.fallbackArgsSlot).Load(NWB_LIGHT_SPACE_FALLBACK_COUNT_OFFSET);
    if(itemIndex >= fallbackCount)
        return;
    const uint packedPixel = nwbHeapRawBuffer(g_NwbLightSpacePush.fallbackListSlot).Load(itemIndex * NWB_LIGHT_SPACE_PIXEL_WORD_BYTES);
    const uint2 pixel = uint2(packedPixel & 0xffffu, packedPixel >> 16u);
    const uint2 fullExtent = uint2(g_NwbLightSpacePush.width, g_NwbLightSpacePush.height);
    const uint2 halfExtent = (fullExtent + g_NwbLightSpacePush.receiverFactor - 1u) / g_NwbLightSpacePush.receiverFactor;
    if(any(pixel >= halfExtent))
        return;
    bool unusedBackground;
    float3 position;
    float3 normal;
    float3 positionError;
    const bool receiverValid = nwbLightSpaceLoadReceiver<false>(pixel, fullExtent, unusedBackground, position, normal, positionError);
    const uint sampleCount = clamp(g_NwbLightSpacePush.sampleCount, 1u, NWB_SW_SHADOW_TRANSPARENT_SPP);
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint index = 0u; index < lightCount; ++index){
        const NwbSceneLight light = g_NwbSceneLights[index];
        if(light.params.z < 0.0)
            continue;
        const uint3 outputPixel = uint3(pixel, uint(light.params.z));
        if(nwbHeapStorageImage2DArrayFloat4(g_NwbLightSpacePush.outputSlot)[outputPixel].a != 0.0)
            continue;
#if NWB_LIGHT_SPACE_CSG_ENABLED
        if(!receiverValid){
            half3 visibility = half3(0.0h);
            visibility = nwbCsgShadowRayFailure(NWB_CSG_RAY_FAILURE_UNSUPPORTED);
            nwbHeapStorageImage2DArrayFloat4(g_NwbLightSpacePush.outputSlot)[outputPixel] = float4(visibility, 1.0);
            continue;
        }
#endif
        half3 result = half3(0.0h);
        for(uint sample = 0u; sample < sampleCount; ++sample){
            const float2 jitter = nwbLightSpaceSampleJitter(pixel, sample);
#if NWB_LIGHT_SPACE_CSG_ENABLED
            half3 visibility = half3(1.0h);
            float3 direction;
            float minimum;
            float maximum;
            if(nwbShadowBuildLightRay(position, normal, light, jitter, direction, minimum, maximum)){
                const float3 origin = position + normal * minimum;
                visibility = nwbCsgShadowRayVisibility(origin, direction, minimum, maximum);
                bool ordinaryRequired = any(visibility > half3(0.0h));
#if NWB_LIGHT_SPACE_OCCLUDER == NWB_SW_SHADOW_OCCLUDER_TRANSPARENT
                ordinaryRequired = ordinaryRequired && (g_NwbLightSpacePush.csgFlags & NWB_CSG_SHADOW_FLAG_NO_ORDINARY_TRANSPARENT) == 0u;
#endif
                if(ordinaryRequired){
#if NWB_LIGHT_SPACE_OCCLUDER == NWB_SW_SHADOW_OCCLUDER_OPAQUE
                    if(nwbSwShadowOpaqueBlocked(origin, direction, minimum, maximum))
                        visibility = half3(0.0h);
#else
                    NwbShadowVisibilityAccumulator accumulator = nwbShadowVisibilityBegin();
                    nwbSwShadowOccluded(origin, direction, minimum, maximum, accumulator);
                    visibility = nwbShadowFinalizeVisibility(accumulator) * visibility;
#endif
                }
            }
#else
            half3 visibility = nwbSwShadowLightVisible(position, normal, light, jitter);
#endif
            result += visibility / half(sampleCount);
        }
        nwbHeapStorageImage2DArrayFloat4(g_NwbLightSpacePush.outputSlot)[outputPixel] = float4(result, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

