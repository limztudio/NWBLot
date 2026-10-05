// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lighting_framework.slangi"
// Cook-generated dispatch module: assembled from every material's `bxdf` declaration (see the asset cooker's
// EmitDeferredBxdfDispatchModule). Material-authored shading stays at the same surface/BXDF boundary while the
// lighting pass executes as compute.
#include "deferred/generated/bxdf_dispatch.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbDeferredLightingOutput nwbHeapStorageImage2DFloat4(g_NwbDeferredBindlessResources.compositeSlots.w)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Direct G-buffer texel loads match target dimensions and avoid compute-derivative feature requirements.
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
    // Scene color is linear transport for both SDR and HDR10. Apply display mapping only after composition.
    const half3 shadedColor = max(resolvedColor, half3(0.0h));
    g_NwbDeferredLightingOutput[pixel] = float4(float3(shadedColor), 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef g_NwbDeferredLightingOutput


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

