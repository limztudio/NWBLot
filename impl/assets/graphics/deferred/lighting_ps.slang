// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "../scene/binding_slots.h"

#define NWB_SCENE_SHADING_SET NWB_SCENE_SHADING_DEFERRED_LIGHTING_SET
#define NWB_SCENE_SHADING_BINDING NWB_SCENE_SHADING_DEFERRED_LIGHTING_BINDING

#include "../scene/lighting.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_BASE_COLOR, NWB_DEFERRED_LIGHTING_SET)]] Texture2D<float4> g_GBufferBaseColor;
[[vk::binding(NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_NORMAL, NWB_DEFERRED_LIGHTING_SET)]] Texture2D<float4> g_GBufferNormal;
[[vk::binding(NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_WORLD_POSITION, NWB_DEFERRED_LIGHTING_SET)]] Texture2D<float4> g_GBufferWorldPosition;
[[vk::binding(NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_DEPTH, NWB_DEFERRED_LIGHTING_SET)]] Texture2D<float4> g_GBufferDepth;
[[vk::binding(NWB_DEFERRED_LIGHTING_BINDING_SAMPLER, NWB_DEFERRED_LIGHTING_SET)]] SamplerState g_DeferredSampler;

struct NwbDeferredLightingPixelInput{
    [[vk::location(NWB_DEFERRED_FULLSCREEN_UV_LOCATION)]] float2 uv : TEXCOORD0;
};

struct NwbDeferredLightingPixelOutput{
    [[vk::location(NWB_DEFERRED_COLOR_TARGET_LOCATION)]] float4 color : SV_Target0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbDeferredDecodeNormal(float3 value){
    return nwbSafeNormalize(value * 2.0 - 1.0, float3(0.0, 0.0, 1.0));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbDeferredLightingPixelOutput main(NwbDeferredLightingPixelInput input){
    NwbDeferredLightingPixelOutput output;

    const float3 baseColor = g_GBufferBaseColor.Sample(g_DeferredSampler, input.uv).rgb;
    const float depth = g_GBufferDepth.Sample(g_DeferredSampler, input.uv).r;
    if(depth >= 0.999999){
        output.color = float4(baseColor, 1.0);
        return output;
    }

    const float3 normal = nwbDeferredDecodeNormal(g_GBufferNormal.Sample(g_DeferredSampler, input.uv).rgb);
    const float3 worldPosition = g_GBufferWorldPosition.Sample(g_DeferredSampler, input.uv).xyz;
    const float3 shadedColor = clamp(
        nwbSceneApplyDirectionalShading(baseColor, normal, float4(0.0, 0.0, 0.0, 1.0), worldPosition),
        float3(0.0),
        float3(1.0)
    );

    output.color = float4(shadedColor, 1.0);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

