// limztudio@gmail.com


#include "binding_slots.h"
#include "bindless_resources.slangi"
#include "../avboit/constants.h"


struct NwbDeferredCompositePushConstants{
    uint resourceSlots;
};

[[vk::push_constant]]
ConstantBuffer<NwbDeferredCompositePushConstants> g_NwbDeferredCompositePushConstants;


#define g_NwbDeferredBindlessResources g_NwbHeapDeferredBindlessResources[g_NwbDeferredCompositePushConstants.resourceSlots]
#define g_NwbDeferredCompositeOutput NwbHeapStorageImage2DFloat4(g_NwbDeferredBindlessResources.avboitWorkSlots1.w)


// Composite stays in linear RGBA16F on Compute. The following Graphics present pass alone handles the swapchain
// framebuffer/sRGB boundary, so this dispatch never requires storage-capable surface images.
[numthreads(NWB_DEFERRED_COMPOSITE_GROUP_SIZE, NWB_DEFERRED_COMPOSITE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    uint width;
    uint height;
    g_NwbDeferredCompositeOutput.GetDimensions(width, height);

    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= width || pixel.y >= height)
        return;

    // Every composite input is a full-resolution point-sampled target. Use direct mip-0 loads so this Compute shader
    // stays free of implicit derivatives and therefore needs no compute-derivative Vulkan extension or feature.
    const int3 texel = int3(pixel, 0);
    const half3 opaqueColor = half3(NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.compositeSlots.x).Load(texel).rgb);
    const half4 transparentColorAccum = half4(NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.compositeSlots.y).Load(texel));
    const half transparentExtinction = half(NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.compositeSlots.z).Load(texel).r);
    const half totalTransmittance = exp(-min(transparentExtinction, half(NWB_AVBOIT_EXTINCTION_SATURATION)));
    const half3 compositeColor = transparentColorAccum.rgb + opaqueColor * totalTransmittance;
    g_NwbDeferredCompositeOutput[pixel] = float4(float3(compositeColor), 1.0);
}


#undef g_NwbDeferredCompositeOutput
#undef g_NwbDeferredBindlessResources


