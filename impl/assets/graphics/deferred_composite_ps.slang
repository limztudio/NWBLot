// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(0, 0)]] Texture2D<float4> g_OpaqueColor;
[[vk::binding(1, 0)]] Texture2D<float4> g_AvboitAccumColor;
[[vk::binding(2, 0)]] Texture2D<float4> g_AvboitAccumExtinction;
[[vk::binding(3, 0)]] SamplerState g_DeferredSampler;

struct NwbDeferredCompositePixelInput{
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct NwbDeferredCompositePixelOutput{
    [[vk::location(0)]] float4 color : SV_Target0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbDeferredCompositePixelOutput main(NwbDeferredCompositePixelInput input){
    const float3 opaqueColor = g_OpaqueColor.Sample(g_DeferredSampler, input.uv).rgb;
    const float4 transparentColorAccum = g_AvboitAccumColor.Sample(g_DeferredSampler, input.uv);
    const float transparentExtinction = g_AvboitAccumExtinction.Sample(g_DeferredSampler, input.uv).r;
    const float totalTransmittance = exp(-min(transparentExtinction, 16.0));

    NwbDeferredCompositePixelOutput output;
    output.color = float4(transparentColorAccum.rgb + opaqueColor * totalTransmittance, 1.0);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

