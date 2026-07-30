// limztudio@gmail.com


#include "binding_slots.h"
#include "bindless_resources.slangi"


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


// Surface images commonly expose ColorAttachment and sampled usage but not storage writes. Composite math therefore
// runs in composite_cs on AsyncCompute, while this tiny Graphics pass samples its linear result into the presentable
// framebuffer and preserves the existing swapchain format conversion.
NwbDeferredCompositePixelOutput main(NwbDeferredCompositePixelInput input){
    const SamplerState sampler = NwbHeapSampler(g_NwbDeferredBindlessResources.lightingSlots.w);
    const half3 compositeColor = half3(NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.avboitWorkSlots1.z).Sample(sampler, input.uv).rgb);

    NwbDeferredCompositePixelOutput output;
    output.color = half4(compositeColor, half(1.0));
    return output;
}

#undef g_NwbDeferredBindlessResources


