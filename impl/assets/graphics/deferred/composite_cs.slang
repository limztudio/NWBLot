// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "bindless_resources.slangi"
#include "../avboit/constants.h"
#include "../reflection/frame_constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbDeferredCompositePushConstants{
    uint resourceSlots;
    uint refractionResources;
    uint opaqueReflectionSlot;
    uint glassReflectionSlot;
    uint reflectionDebugView;
};

[[vk::push_constant]] ConstantBuffer<NwbDeferredCompositePushConstants> g_NwbDeferredCompositePushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbDeferredBindlessResources g_NwbHeapDeferredBindlessResources[g_NwbDeferredCompositePushConstants.resourceSlots]
#define g_NwbDeferredCompositeOutput nwbHeapStorageImage2DFloat4(g_NwbDeferredBindlessResources.avboitWorkSlots1.w)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const half3 NWB_DEFERRED_REFLECTION_DEBUG_HARDWARE_COLOR = half3(0.15h, 0.3h, 1.0h);
static const half3 NWB_DEFERRED_REFLECTION_DEBUG_SCREEN_COLOR = half3(0.15h, 1.0h, 0.2h);
static const half3 NWB_DEFERRED_REFLECTION_DEBUG_FALLBACK_COLOR = half3(1.0h, 0.25h, 0.1h);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Composite stays in linear RGBA16F on Compute. The following Graphics present pass handles the swapchain/sRGB boundary, so this dispatch needs no storage-capable surface images.
[numthreads(NWB_DEFERRED_COMPOSITE_GROUP_SIZE, NWB_DEFERRED_COMPOSITE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    uint width;
    uint height;
    g_NwbDeferredCompositeOutput.GetDimensions(width, height);

    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= width || pixel.y >= height)
        return;

    // Every composite input is a full-resolution point-sampled target. Use direct mip-0 loads so this Compute shader needs no implicit derivatives or compute-derivative extension.
    const int3 texel = int3(pixel, 0);
    const half4 opaqueReflection = g_NwbDeferredCompositePushConstants.opaqueReflectionSlot != NWB_DEFERRED_INVALID_HEAP_SLOT ? half4(nwbHeapSampledImage2D(g_NwbDeferredCompositePushConstants.opaqueReflectionSlot).Load(texel)) : half4(0.0h);
    const half4 glassReflection = g_NwbDeferredCompositePushConstants.glassReflectionSlot != NWB_DEFERRED_INVALID_HEAP_SLOT ?half4(nwbHeapSampledImage2D(g_NwbDeferredCompositePushConstants.glassReflectionSlot).Load(texel)) : half4(0.0h);
    const half3 opaqueColor = half3(nwbHeapSampledImage2D(g_NwbDeferredBindlessResources.compositeSlots.x).Load(texel).rgb) + opaqueReflection.rgb;
    half reflectionSource = opaqueReflection.a;
    const half4 transparentColorAccum = half4(nwbHeapSampledImage2D(g_NwbDeferredBindlessResources.compositeSlots.y).Load(texel));
    const half transparentExtinction = half(nwbHeapSampledImage2D(g_NwbDeferredBindlessResources.compositeSlots.z).Load(texel).r);
    const half totalTransmittance = exp(-min(transparentExtinction, half(NWB_AVBOIT_EXTINCTION_SATURATION)));
    half3 compositeColor = transparentColorAccum.rgb + opaqueColor * totalTransmittance;
    if(g_NwbDeferredCompositePushConstants.refractionResources != 0u){
        // Current capture depth is the validity contract for sparsely written resolve tiles.
        // Empty tiles retain ordinary AVBOIT composition and never consume old or uninitialized radiance.
        const float refractionDepth = nwbHeapSampledImage2D(g_NwbDeferredBindlessResources.refractionSlots0.x).Load(texel).r;
        if(refractionDepth < 1.0){
            // Front-layer color and its (0, 1) bounded extinction/transmittance factor are LDR color math, matching the half composite path.
            const half3 frontColor = half3(nwbHeapSampledImage2D(g_NwbDeferredBindlessResources.refractionSlots1.y).Load(texel).rgb);
            const half frontExtinction = half(nwbHeapSampledImage2D(g_NwbDeferredBindlessResources.refractionSlots1.z).Load(texel).r);
            const half frontTransmittance = exp(-min(frontExtinction, half(NWB_AVBOIT_EXTINCTION_SATURATION)));
            const half4 transmitted = half4(nwbHeapSampledImage2D(g_NwbDeferredBindlessResources.refractionSlots1.x).Load(texel));
            const half3 transmittedColor = transmitted.a > half(0.0h) ? frontColor + frontTransmittance * (transmitted.rgb + glassReflection.rgb) : frontColor + transparentColorAccum.rgb + opaqueColor * frontTransmittance * totalTransmittance;
            compositeColor = transmittedColor;
        }
    }
    if(glassReflection.a > half(0.0h))
        reflectionSource = glassReflection.a;
    if(g_NwbDeferredCompositePushConstants.reflectionDebugView == NWB_REFLECTION_DEBUG_VIEW_TRACE_SOURCE){
        compositeColor = reflectionSource >= half(NWB_REFLECTION_SOURCE_HARDWARE)
            ? NWB_DEFERRED_REFLECTION_DEBUG_HARDWARE_COLOR
            : reflectionSource >= half(NWB_REFLECTION_SOURCE_SCREEN)
                ? NWB_DEFERRED_REFLECTION_DEBUG_SCREEN_COLOR
                : reflectionSource > half(0.0h)
                    ? NWB_DEFERRED_REFLECTION_DEBUG_FALLBACK_COLOR
                    : half3(0.0h)
        ;
    }
    else if(g_NwbDeferredCompositePushConstants.reflectionDebugView == NWB_REFLECTION_DEBUG_VIEW_CONFIDENCE)
        compositeColor = half3(reflectionSource >= half(NWB_REFLECTION_SOURCE_HARDWARE) ? half(1.0h) : saturate((reflectionSource - half(NWB_REFLECTION_SOURCE_SCREEN)) * half(NWB_REFLECTION_SOURCE_CONFIDENCE_DECODE_SCALE)));
    g_NwbDeferredCompositeOutput[pixel] = float4(float3(compositeColor), 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef g_NwbDeferredCompositeOutput
#undef g_NwbDeferredBindlessResources


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

