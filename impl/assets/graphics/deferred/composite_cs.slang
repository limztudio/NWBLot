// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "bindless_resources.slangi"
#include "../avboit/constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbDeferredCompositePushConstants{
    uint resourceSlots;
    uint refractionResources;
    uint opaqueReflectionSlot;
    uint glassReflectionSlot;
    uint reflectionDebugView;
};

[[vk::push_constant]]
ConstantBuffer<NwbDeferredCompositePushConstants> g_NwbDeferredCompositePushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbDeferredBindlessResources g_NwbHeapDeferredBindlessResources[g_NwbDeferredCompositePushConstants.resourceSlots]
#define g_NwbDeferredCompositeOutput NwbHeapStorageImage2DFloat4(g_NwbDeferredBindlessResources.avboitWorkSlots1.w)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    const float4 opaqueReflection = g_NwbDeferredCompositePushConstants.opaqueReflectionSlot != 0xffffffffu
        ? NwbHeapSampledImage2D(g_NwbDeferredCompositePushConstants.opaqueReflectionSlot).Load(texel) : float4(0.0);
    const float4 glassReflection = g_NwbDeferredCompositePushConstants.glassReflectionSlot != 0xffffffffu
        ? NwbHeapSampledImage2D(g_NwbDeferredCompositePushConstants.glassReflectionSlot).Load(texel) : float4(0.0);
    const half3 opaqueColor = half3(NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.compositeSlots.x).Load(texel).rgb + opaqueReflection.rgb);
    float reflectionSource = opaqueReflection.a;
    const half4 transparentColorAccum = half4(NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.compositeSlots.y).Load(texel));
    const half transparentExtinction = half(NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.compositeSlots.z).Load(texel).r);
    const half totalTransmittance = exp(-min(transparentExtinction, half(NWB_AVBOIT_EXTINCTION_SATURATION)));
    half3 compositeColor = transparentColorAccum.rgb + opaqueColor * totalTransmittance;
    if(g_NwbDeferredCompositePushConstants.refractionResources != 0u){
        const float3 frontColor = NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.refractionSlots1.y).Load(texel).rgb;
        const float frontExtinction = NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.refractionSlots1.z).Load(texel).r;
        const float frontTransmittance = exp(-min(frontExtinction, float(NWB_AVBOIT_EXTINCTION_SATURATION)));
        const float4 transmitted = NwbHeapSampledImage2D(g_NwbDeferredBindlessResources.refractionSlots1.x).Load(texel);
        compositeColor = half3(transmitted.a > 0.0
            ? frontColor + frontTransmittance * (transmitted.rgb + glassReflection.rgb)
            : frontColor + float3(transparentColorAccum.rgb) + float3(opaqueColor) * frontTransmittance * float(totalTransmittance));
    }
    if(glassReflection.a > 0.0)
        reflectionSource = glassReflection.a;
    if(g_NwbDeferredCompositePushConstants.reflectionDebugView == 1u){
        compositeColor = reflectionSource >= 3.0 ? half3(0.15h, 0.3h, 1.0h)
            : reflectionSource >= 2.0 ? half3(0.15h, 1.0h, 0.2h)
            : reflectionSource > 0.0 ? half3(1.0h, 0.25h, 0.1h) : half3(0.0h);
    }
    else if(g_NwbDeferredCompositePushConstants.reflectionDebugView == 2u)
        compositeColor = half3(reflectionSource >= 3.0 ? 1.0 : saturate((reflectionSource - 2.0) * 2.0));
    g_NwbDeferredCompositeOutput[pixel] = float4(float3(compositeColor), 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef g_NwbDeferredCompositeOutput
#undef g_NwbDeferredBindlessResources


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

