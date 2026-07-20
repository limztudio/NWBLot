#include "binding_slots.h"
#include "../avboit/constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_DEFERRED_COMPOSITE_BINDING_OPAQUE_COLOR, NWB_DEFERRED_COMPOSITE_SET)]] Texture2D<float4> g_OpaqueColor;
[[vk::binding(NWB_DEFERRED_COMPOSITE_BINDING_AVBOIT_ACCUM_COLOR, NWB_DEFERRED_COMPOSITE_SET)]] Texture2D<float4> g_AvboitAccumColor;
[[vk::binding(NWB_DEFERRED_COMPOSITE_BINDING_AVBOIT_ACCUM_EXTINCTION, NWB_DEFERRED_COMPOSITE_SET)]] Texture2D<float4> g_AvboitAccumExtinction;
[[vk::binding(NWB_DEFERRED_COMPOSITE_BINDING_SAMPLER, NWB_DEFERRED_COMPOSITE_SET)]] SamplerState g_DeferredSampler;

struct NwbDeferredCompositePixelInput{
    [[vk::location(NWB_DEFERRED_FULLSCREEN_UV_LOCATION)]] float2 uv : TEXCOORD0;
};

struct NwbDeferredCompositePixelOutput{
    [[vk::location(NWB_DEFERRED_COLOR_TARGET_LOCATION)]] half4 color : SV_Target0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbDeferredCompositePixelOutput main(NwbDeferredCompositePixelInput input){
    const half3 opaqueColor = half3(g_OpaqueColor.Sample(g_DeferredSampler, input.uv).rgb);
    const half4 transparentColorAccum = half4(g_AvboitAccumColor.Sample(g_DeferredSampler, input.uv));
    const half transparentExtinction = half(g_AvboitAccumExtinction.Sample(g_DeferredSampler, input.uv).r);
    const half totalTransmittance = exp(-min(transparentExtinction, half(NWB_AVBOIT_EXTINCTION_SATURATION)));
    const half3 compositeColor = transparentColorAccum.rgb + opaqueColor * totalTransmittance;

    NwbDeferredCompositePixelOutput output;
    output.color = half4(compositeColor, half(1.0));
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

