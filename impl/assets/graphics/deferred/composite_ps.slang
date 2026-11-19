// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "bindless_resources.slangi"
#include "../common/hdr10.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbDeferredCompositePushConstants{
    uint resourceSlots;
    uint presentationMode;
    uint toneMap;
    float exposure;
    float shoulder;
    uint outputLayerSlot;
};

[[vk::push_constant]] ConstantBuffer<NwbDeferredCompositePushConstants> g_NwbDeferredCompositePushConstants;

#define g_NwbDeferredBindlessResources g_NwbHeapDeferredBindlessResources[g_NwbDeferredCompositePushConstants.resourceSlots]

static const half NWB_DEFERRED_FINITE_HALF_MAX = 65504.0h;

struct NwbDeferredCompositePixelInput{
    [[vk::location(NWB_DEFERRED_FULLSCREEN_UV_LOCATION)]] float2 uv : TEXCOORD0;
};

struct NwbDeferredCompositePixelOutput{
    [[vk::location(NWB_DEFERRED_COLOR_TARGET_LOCATION)]] float4 color : SV_Target0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbDeferredCompositePixelOutput main(NwbDeferredCompositePixelInput input){
    const SamplerState sampler = nwbHeapSampler(g_NwbDeferredBindlessResources.lightingSlots.w);
    const half3 compositeColor = half3(nwbHeapSampledImage2D(g_NwbDeferredBindlessResources.avboitWorkSlots1.z).Sample(sampler, input.uv).rgb);
    const float3 exposedColor = min(float3(clamp(compositeColor, half3(0.0h), half3(NWB_DEFERRED_FINITE_HALF_MAX))) * g_NwbDeferredCompositePushConstants.exposure, float3(NWB_DEFERRED_FINITE_HALF_MAX));
    const float3 sdrColor = g_NwbDeferredCompositePushConstants.toneMap == NWB_DEFERRED_TONE_MAP_REINHARD ? exposedColor / (exposedColor + g_NwbDeferredCompositePushConstants.shoulder) : saturate(exposedColor);

    float4 outputLayer = float4(0.0);
    if(g_NwbDeferredCompositePushConstants.outputLayerSlot != NWB_DEFERRED_INVALID_HEAP_SLOT)
        outputLayer = nwbHeapSampledImage2D(g_NwbDeferredCompositePushConstants.outputLayerSlot).Sample(sampler, input.uv);

    NwbDeferredCompositePixelOutput output;
    const half3 presentationColor = g_NwbDeferredCompositePushConstants.presentationMode == NWB_DEFERRED_PRESENTATION_HDR10
        ? half3(nwbHdr10EncodeSceneWithUi(float3(exposedColor), outputLayer.rgb, outputLayer.a))
        : half3(nwbOutputLayerOver(float3(sdrColor), outputLayer.rgb, outputLayer.a))
    ;
    output.color = half4(presentationColor, half(1.0h));
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef g_NwbDeferredBindlessResources


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

