// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SCENE_SHADING_SET 0
#define NWB_SCENE_SHADING_BINDING 5

#include <bxdf_lighting.slangi>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(0, 0)]] Texture2D<float4> g_GBufferBaseColor;
[[vk::binding(1, 0)]] Texture2D<float4> g_GBufferNormal;
[[vk::binding(2, 0)]] Texture2D<float4> g_GBufferWorldPosition;
[[vk::binding(3, 0)]] Texture2D<float4> g_GBufferDepth;
[[vk::binding(4, 0)]] SamplerState g_DeferredSampler;

struct NwbDeferredLightingPixelInput{
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct NwbDeferredLightingPixelOutput{
    [[vk::location(0)]] float4 color : SV_Target0;
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
        nwbProjectApplyDirectionalShading(baseColor, normal, float4(0.0, 0.0, 0.0, 1.0), worldPosition),
        float3(0.0),
        float3(1.0)
    );

    output.color = float4(shadedColor, 1.0);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

