// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lighting_framework.slangi"
// Cook-generated dispatch module: assembled from every material's `bxdf` declaration (see the asset cooker's
// EmitDeferredBxdfDispatchModule). It is shared with the legacy fullscreen harness, so material-authored shading
// stays bit-for-bit at the same surface/BXDF boundary while the execution lane becomes compute-capable.
#include "deferred/generated/bxdf_dispatch.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbDeferredLightingOutput NwbHeapStorageImage2DFloat4(g_NwbDeferredBindlessResources.compositeSlots.w)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deferred lighting is now a screen-space compute dispatch. The target dimensions define the exact pixel domain;
// deriving uv from texel centres preserves the old fullscreen pixel shader's point-sampled G-buffer coordinates.
[numthreads(NWB_DEFERRED_LIGHTING_GROUP_SIZE, NWB_DEFERRED_LIGHTING_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    uint width;
    uint height;
    g_NwbDeferredLightingOutput.GetDimensions(width, height);

    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= width || pixel.y >= height)
        return;

    const float2 uv = (float2(pixel) + float2(0.5, 0.5)) / float2(width, height);
    const NwbBxdfSurface surface = nwbBxdfLoadSurface(uv);
    const half3 shadedColor = surface.isBackground
        ? surface.baseColor
        : clamp(
            nwbDeferredDispatchBxdf(surface.shadingModel, surface, int2(pixel)),
            half3(0.0h),
            half3(1.0h)
        )
    ;
    g_NwbDeferredLightingOutput[pixel] = float4(float3(shadedColor), 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef g_NwbDeferredLightingOutput
