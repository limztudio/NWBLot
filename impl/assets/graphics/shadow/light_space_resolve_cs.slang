// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_LIGHT_SPACE_CSG_ENABLED
#define NWB_LIGHT_SPACE_CSG_ENABLED 0
#endif
#ifndef NWB_CSG_SHADOW_HW_TRACE
#define NWB_CSG_SHADOW_HW_TRACE 0
#endif
#define NWB_CSG_RAY_HARDWARE NWB_CSG_SHADOW_HW_TRACE

#define NWB_LIGHT_SPACE_MAP_ONLY 1
#include "light_space_resolve.slangi"
#if NWB_LIGHT_SPACE_CSG_ENABLED
#include "csg_shadow_diagnostics.slangi"
#if NWB_CSG_SHADOW_HW_TRACE
#include "csg_shadow_fallback.slangi"
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_LIGHT_SPACE_RESOLVE_LANE_COUNT (NWB_LIGHT_SPACE_GROUP_SIZE * NWB_LIGHT_SPACE_GROUP_SIZE)
#if NWB_LIGHT_SPACE_RESOLVE_LANE_COUNT != NWB_LIGHT_SPACE_FALLBACK_GROUP_SIZE
#error "Light-space fallback compaction requires a 64-lane resolve workgroup"
#endif

groupshared uint g_NwbLightSpaceFallbackMasks[2];
groupshared uint g_NwbLightSpaceFallbackBase;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_LIGHT_SPACE_GROUP_SIZE, NWB_LIGHT_SPACE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex){
    const bool compactFallback = g_NwbLightSpacePush.fallbackArgsSlot != NWB_BINDLESS_HEAP_INVALID_SLOT && g_NwbLightSpacePush.fallbackListSlot != NWB_BINDLESS_HEAP_INVALID_SLOT;
    if(compactFallback && groupIndex < 2u)
        g_NwbLightSpaceFallbackMasks[groupIndex] = 0u;
    const uint2 pixel = dispatchThreadID.xy;
    const uint2 fullExtent = uint2(g_NwbLightSpacePush.width, g_NwbLightSpacePush.height);
    const uint2 halfExtent = (fullExtent + g_NwbLightSpacePush.receiverFactor - 1u) / g_NwbLightSpacePush.receiverFactor;
    bool anyRequiresFallback = false;
    if(all(pixel < halfExtent)){
        bool background;
        float3 position;
        float3 normal;
        float3 positionError;
        const bool receiverValid = nwbLightSpaceLoadReceiver<true>(pixel, fullExtent, background, position, normal, positionError);
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
                    const float2 jitter = nwbLightSpaceSampleJitter(pixel, sample);
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
            if(requiresFallback)
                anyRequiresFallback = true;
        }
    }
    if(compactFallback){
        GroupMemoryBarrierWithGroupSync();
        const uint maskIndex = groupIndex >> 5u;
        const uint bit = 1u << (groupIndex & 31u);
        if(anyRequiresFallback)
            InterlockedOr(g_NwbLightSpaceFallbackMasks[maskIndex], bit);
        GroupMemoryBarrierWithGroupSync();
        if(groupIndex == 0u){
            const uint localCount = countbits(g_NwbLightSpaceFallbackMasks[0]) + countbits(g_NwbLightSpaceFallbackMasks[1]);
            g_NwbLightSpaceFallbackBase = 0u;
            if(localCount != 0u){
                uint baseIndex = 0u;
                g_NwbHeapStorageBuffers[g_NwbLightSpacePush.fallbackArgsSlot].InterlockedAdd(NWB_LIGHT_SPACE_FALLBACK_COUNT_OFFSET, localCount, baseIndex);
                g_NwbLightSpaceFallbackBase = baseIndex;
                const uint neededGroups = (baseIndex + localCount + NWB_LIGHT_SPACE_FALLBACK_GROUP_SIZE - 1u) / NWB_LIGHT_SPACE_FALLBACK_GROUP_SIZE;
                uint previousGroups = 0u;
                g_NwbHeapStorageBuffers[g_NwbLightSpacePush.fallbackArgsSlot].InterlockedMax(0u, neededGroups, previousGroups);
            }
        }
        GroupMemoryBarrierWithGroupSync();
        if(anyRequiresFallback){
            const uint prefix = countbits(g_NwbLightSpaceFallbackMasks[maskIndex] & (bit - 1u)) + (maskIndex == 0u ? 0u : countbits(g_NwbLightSpaceFallbackMasks[0]));
            const uint listIndex = g_NwbLightSpaceFallbackBase + prefix;
            const uint packedPixel = (pixel.x & 0xffffu) | ((pixel.y & 0xffffu) << 16u);
            g_NwbHeapStorageBuffers[g_NwbLightSpacePush.fallbackListSlot].Store(listIndex * NWB_LIGHT_SPACE_PIXEL_WORD_BYTES, packedPixel);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

