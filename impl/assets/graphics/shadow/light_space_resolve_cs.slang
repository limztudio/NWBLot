// limztudio@gmail.com


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_LIGHT_SPACE_CSG_ENABLED
#define NWB_LIGHT_SPACE_CSG_ENABLED 0
#endif
#ifndef NWB_CSG_SHADOW_HW_TRACE
#define NWB_CSG_SHADOW_HW_TRACE 0
#endif

#define NWB_LIGHT_SPACE_MAP_ONLY 1
#include "light_space_resolve.slangi"
#if NWB_LIGHT_SPACE_CSG_ENABLED
#include "csg_shadow_diagnostics.slangi"
#if NWB_CSG_SHADOW_HW_TRACE
#include "csg_shadow_fallback.slangi"
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_LIGHT_SPACE_GROUP_SIZE, NWB_LIGHT_SPACE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    const uint2 fullExtent = uint2(g_NwbLightSpacePush.width, g_NwbLightSpacePush.height);
    const uint2 halfExtent = (fullExtent + g_NwbLightSpacePush.receiverFactor - 1u) / g_NwbLightSpacePush.receiverFactor;
    if(any(pixel >= halfExtent))
        return;
#if NWB_LIGHT_SPACE_CSG_ENABLED
    const uint2 receiverPixel = min(pixel * g_NwbLightSpacePush.receiverFactor, fullExtent - 1u);
    const int3 texel = int3(receiverPixel, 0);
    const float depth = nwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.w).Load(texel).r;
    const bool background = depth >= NWB_SW_SHADOW_BACKGROUND_DEPTH;
    float3 position = float3(0.0);
    float3 positionError = float3(0.0);
    const bool receiverValid = background || nwbLightSpaceReceiverPosition(receiverPixel, depth, position, positionError);
#else
    const int3 texel = int3(min(pixel * g_NwbLightSpacePush.receiverFactor, fullExtent - 1u), 0);
    const bool background = nwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.w).Load(texel).r >= NWB_SW_SHADOW_BACKGROUND_DEPTH;
    const float3 position = nwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.z).Load(texel).xyz;
    const float3 positionError = float3(0.0);
#endif
    const float3 normal = normalize(nwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.y).Load(texel).xyz * 2.0 - 1.0);
    const uint sampleCount = clamp(g_NwbLightSpacePush.sampleCount, 1u, NWB_SW_SHADOW_TRANSPARENT_SPP);
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint index = 0u; index < lightCount; ++index){
        const NwbSceneLight light = g_NwbSceneLights[index];
        if(light.params.z < 0.0)
            continue;
        half3 result = half3(1.0h);
        bool requiresFallback = false;
#if NWB_LIGHT_SPACE_CSG_ENABLED
        if(!receiverValid){
            result = half3(0.0h);
            result = nwbCsgShadowRayFailure(NWB_CSG_RAY_FAILURE_UNSUPPORTED);
        }
        else
#endif
        if(!background){
            const float3 origin = position + normal * NWB_SHADOW_RAY_MIN_DISTANCE;
            NwbLightSpaceView centerView;
            float blockerDistance = 0.0;
            bool mapReady = nwbLightSpaceFindView(index, origin, centerView);
            const bool soft = light.params.y < NWB_SCENE_LIGHT_TYPE_DIRECTIONAL_MAX ? light.params2.x > 0.0 : light.params2.y > 0.0;
            if(mapReady && soft)
                mapReady = nwbLightSpaceBlockerDistance(index, origin, normal, positionError, light, centerView, blockerDistance);
            result = half3(0.0h);
            for(uint sample = 0u; sample < sampleCount; ++sample){
#if NWB_LIGHT_SPACE_OCCLUDER == NWB_SW_SHADOW_OCCLUDER_TRANSPARENT
                const uint salt = NWB_SW_SHADOW_TRANSPARENT_JITTER_SALT;
#else
                const uint salt = 0u;
#endif
                const float2 jitter = nwbShadowLowDiscrepancy2D(pixel, g_NwbLightSpacePush.frameIndex * NWB_SW_SHADOW_TRANSPARENT_SPP + sample + salt);
                float3 direction;
                float minimum;
                float maximum;
                half3 visibility = half3(1.0h);
                if(nwbShadowBuildLightRay(position, normal, light, jitter, direction, minimum, maximum)){
                    if(!mapReady || !nwbLightSpaceEvaluate(index, origin, normal, positionError, direction, blockerDistance, visibility)){
#if NWB_LIGHT_SPACE_CSG_ENABLED && NWB_CSG_SHADOW_HW_TRACE
                        if(nwbCsgShadowHardwareCompose())
                            visibility = nwbCsgShadowRayVisibility(origin, direction, minimum, maximum);
                        else
#endif
                        {
                            requiresFallback = true;
                            break;
                        }
                    }
                }
                result += visibility / half(sampleCount);
            }
        }
        const uint3 outputPixel = uint3(pixel, uint(light.params.z));
#if NWB_LIGHT_SPACE_CSG_ENABLED && NWB_CSG_SHADOW_HW_TRACE
        bool compose = nwbCsgShadowHardwareCompose();
#if NWB_LIGHT_SPACE_OCCLUDER == NWB_SW_SHADOW_OCCLUDER_TRANSPARENT
        compose = compose && (g_NwbLightSpacePush.csgFlags & NWB_CSG_SHADOW_FLAG_NO_ORDINARY_TRANSPARENT) == 0u;
#endif
        if(compose)
            result *= half3(nwbHeapStorageImage2DArrayFloat4(g_NwbLightSpacePush.outputSlot)[outputPixel].rgb);
#endif
        nwbHeapStorageImage2DArrayFloat4(g_NwbLightSpacePush.outputSlot)[outputPixel] = float4(result, requiresFallback ? 0.0 : 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

