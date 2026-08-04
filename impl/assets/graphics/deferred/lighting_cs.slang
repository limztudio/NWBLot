// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lighting_framework.slangi"
// Cook-generated dispatch module: assembled from every material's `bxdf` declaration (see the asset cooker's
// EmitDeferredBxdfDispatchModule). Material-authored shading stays at the same surface/BXDF boundary while the
// lighting pass executes as compute.
#include "deferred/generated/bxdf_dispatch.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbDeferredLightingOutput NwbHeapStorageImage2DFloat4(g_NwbDeferredBindlessResources.compositeSlots.w)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deferred lighting is now a screen-space compute dispatch. The target dimensions define the exact pixel domain; the
// full-resolution G-buffer is point sampled, so use its texel coordinate directly. This avoids implicit texture
// sampling in Compute, which would otherwise require compute-derivative Vulkan features.
[numthreads(NWB_DEFERRED_LIGHTING_GROUP_SIZE, NWB_DEFERRED_LIGHTING_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    uint width;
    uint height;
    g_NwbDeferredLightingOutput.GetDimensions(width, height);

    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= width || pixel.y >= height)
        return;

    const NwbBxdfSurface surface = nwbBxdfLoadSurface(int2(pixel));
    const half3 resolvedColor = surface.isBackground
        ? surface.baseColor
        : nwbDeferredDispatchBxdf(surface.shadingModel, surface, int2(pixel))
    ;
    // SDR keeps the established LDR clamp. HDR10 carries values above one through the RGBA16F intermediate so
    // highlights only roll off at the final PQ presentation transform.
    const half3 shadedColor = nwbDeferredPresentationIsHdr10()
        ? max(resolvedColor, half3(0.0h))
        : clamp(resolvedColor, half3(0.0h), half3(1.0h))
    ;
    g_NwbDeferredLightingOutput[pixel] = float4(float3(shadedColor), 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef g_NwbDeferredLightingOutput


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

