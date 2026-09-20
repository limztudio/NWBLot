// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_LIGHT_SPACE_MAP_ONLY 1
#include "light_space_resolve.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_LIGHT_SPACE_GROUP_SIZE, NWB_LIGHT_SPACE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    const uint2 halfExtent = (uint2(g_NwbLightSpacePush.width, g_NwbLightSpacePush.height) + 1u) / 2u;
    if(any(pixel >= halfExtent))
        return;
    const int3 texel = int3(min(pixel * 2u, uint2(g_NwbLightSpacePush.width, g_NwbLightSpacePush.height) - 1u), 0);
    const bool background = NwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.w).Load(texel).r >= NWB_SW_SHADOW_BACKGROUND_DEPTH;
    const float3 position = NwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.z).Load(texel).xyz;
    const float3 normal = normalize(NwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.y).Load(texel).xyz * 2.0 - 1.0);
    const uint sampleCount = clamp(g_NwbLightSpacePush.sampleCount, 1u, NWB_SW_SHADOW_TRANSPARENT_SPP);
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint index = 0u; index < lightCount; ++index){
        const NwbSceneLight light = g_NwbSceneLights[index];
        if(light.params.z < 0.0)
            continue;
        float3 result = float3(1.0);
        bool requiresFallback = false;
        if(!background){
            const float3 origin = position + normal * NWB_SHADOW_RAY_MIN_DISTANCE;
            NwbLightSpaceView centerView;
            float blockerDistance = 0.0;
            bool mapReady = nwbLightSpaceFindView(index, origin, centerView);
            const bool soft = light.params.y < NWB_SCENE_LIGHT_TYPE_DIRECTIONAL_MAX ? light.params2.x > 0.0 : light.params2.y > 0.0;
            if(mapReady && soft)
                mapReady = nwbLightSpaceBlockerDistance(index, origin, normal, light, centerView, blockerDistance);
            result = float3(0.0);
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
                float3 visibility = float3(1.0);
                if(nwbShadowBuildLightRay(position, normal, light, jitter, direction, minimum, maximum)){
                    if(!mapReady || !nwbLightSpaceEvaluate(index, origin, normal, direction, blockerDistance, visibility)){
                        requiresFallback = true;
                        break;
                    }
                }
                result += visibility / float(sampleCount);
            }
        }
        NwbHeapStorageImage2DArrayFloat4(g_NwbLightSpacePush.outputSlot)[uint3(pixel, uint(light.params.z))] = float4(result, requiresFallback ? 0.0 : 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

