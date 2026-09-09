// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "presentation_constants.h"
#include "bindless_resources.slangi"
#include "../common/hdr10.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbDeferredCompositePushConstants{
    uint resourceSlots;
    uint presentationMode;
    uint toneMap;
    float exposure;
    float shoulder;
};

[[vk::push_constant]]
ConstantBuffer<NwbDeferredCompositePushConstants> g_NwbDeferredCompositePushConstants;

#define g_NwbDeferredBindlessResources g_NwbHeapDeferredBindlessResources[g_NwbDeferredCompositePushConstants.resourceSlots]

struct NwbDeferredCompositePixelInput{
    [[vk::location(NWB_DEFERRED_FULLSCREEN_UV_LOCATION)]] float2 uv : TEXCOORD0;
};

struct NwbDeferredCompositePixelOutput{
    [[vk::location(NWB_DEFERRED_COLOR_TARGET_LOCATION)]] float4 color : SV_Target0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surface images commonly expose ColorAttachment and sampled usage but not storage writes. Composite math therefore
// runs in composite_cs on AsyncCompute, while this tiny Graphics pass samples its linear result into the presentable
// framebuffer. SDR uses the attachment's sRGB conversion; HDR10 explicitly converts Rec.709 scene color to
// Rec.2020/PQ before writing its A2B10G10R10 surface.
NwbDeferredCompositePixelOutput main(NwbDeferredCompositePixelInput input){
    const SamplerState sampler = NwbHeapSampler(g_NwbDeferredBindlessResources.lightingSlots.w);
    // Composite color is linear RGBA16F. Exposure and display mapping happen once, after every transport effect.
    const half3 compositeColor = half3(NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.avboitWorkSlots1.z).Sample(sampler, input.uv).rgb);
    // Bound extreme finite exposure controls to the representable scene range before HDR conversion or division.
    const float3 exposedColor = min(
        clamp(float3(compositeColor), float3(0.0), float3(65504.0)) * g_NwbDeferredCompositePushConstants.exposure,
        float3(65504.0)
    );
    const float3 sdrColor = g_NwbDeferredCompositePushConstants.toneMap == NWB_DEFERRED_TONE_MAP_REINHARD
        ? exposedColor / (exposedColor + g_NwbDeferredCompositePushConstants.shoulder)
        : saturate(exposedColor)
    ;

    NwbDeferredCompositePixelOutput output;
    const float3 presentationColor = g_NwbDeferredCompositePushConstants.presentationMode == NWB_DEFERRED_PRESENTATION_HDR10
        ? nwbHdr10EncodeScene(exposedColor)
        : sdrColor
    ;
    output.color = half4(half3(presentationColor), half(1.0h));
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef g_NwbDeferredBindlessResources


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

