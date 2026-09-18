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
    // Exposure color math stays in FP16: inputs/outputs are RGBA16F and the 65504 clamp matches half max.
    const half3 exposedColor = min(
        clamp(compositeColor, half3(0.0h), half3(65504.0h)) * half(g_NwbDeferredCompositePushConstants.exposure),
        half3(65504.0h)
    );
    const half3 sdrColor = g_NwbDeferredCompositePushConstants.toneMap == NWB_DEFERRED_TONE_MAP_REINHARD
        ? exposedColor / (exposedColor + half(g_NwbDeferredCompositePushConstants.shoulder))
        : saturate(exposedColor)
    ;

    NwbDeferredCompositePixelOutput output;
    // HDR10 PQ needs FP32 precision, so widen only for the encode and narrow back to the half presentation color.
    const half3 presentationColor = g_NwbDeferredCompositePushConstants.presentationMode == NWB_DEFERRED_PRESENTATION_HDR10
        ? half3(nwbHdr10EncodeScene(float3(exposedColor)))
        : sdrColor
    ;
    output.color = half4(presentationColor, half(1.0h));
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef g_NwbDeferredBindlessResources


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

