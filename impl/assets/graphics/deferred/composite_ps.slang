// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "bindless_resources.slangi"
#include "../avboit/constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbDeferredCompositePushConstants{
    uint resourceSlots;
};

[[vk::push_constant]]
ConstantBuffer<NwbDeferredCompositePushConstants> g_NwbDeferredCompositePushConstants;

#define g_NwbDeferredBindlessResources g_NwbHeapDeferredBindlessResources[g_NwbDeferredCompositePushConstants.resourceSlots]

struct NwbDeferredCompositePixelInput{
    [[vk::location(NWB_DEFERRED_FULLSCREEN_UV_LOCATION)]] float2 uv : TEXCOORD0;
};

struct NwbDeferredCompositePixelOutput{
    [[vk::location(NWB_DEFERRED_COLOR_TARGET_LOCATION)]] half4 color : SV_Target0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbDeferredCompositePixelOutput main(NwbDeferredCompositePixelInput input){
    const SamplerState sampler = NwbHeapSampler(g_NwbDeferredBindlessResources.lightingSlots.w);
    const half3 opaqueColor = half3(NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.compositeSlots.x).Sample(sampler, input.uv).rgb);
    const half4 transparentColorAccum = half4(NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.compositeSlots.y).Sample(sampler, input.uv));
    const half transparentExtinction = half(NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.compositeSlots.z).Sample(sampler, input.uv).r);
    const half totalTransmittance = exp(-min(transparentExtinction, half(NWB_AVBOIT_EXTINCTION_SATURATION)));
    const half3 compositeColor = transparentColorAccum.rgb + opaqueColor * totalTransmittance;

    NwbDeferredCompositePixelOutput output;
    output.color = half4(compositeColor, half(1.0));
    return output;
}

#undef g_NwbDeferredBindlessResources


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

