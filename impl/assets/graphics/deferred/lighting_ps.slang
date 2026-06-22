// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lighting_framework.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Engine default deferred lighting shader: load the surface the geometry pass packed into the G-buffer and
// shade it with the engine's built-in whole-surface model (shading model 0). A project overrides this shader
// (built on the same framework) to dispatch on surface.shadingModel to its own per-material BXDFs.
NwbDeferredLightingPixelOutput main(NwbDeferredLightingPixelInput input){
    NwbDeferredLightingPixelOutput output;

    const NwbBxdfSurface surface = nwbBxdfLoadSurface(input.uv);
    if(surface.isBackground){
        output.color = float4(surface.baseColor, 1.0);
        return output;
    }

    const uint shadowMask = nwbBxdfLoadShadowMask(int2(input.position.xy));
    const float3 shadedColor = clamp(
        nwbSceneApplyLighting(surface.baseColor, surface.normal, float4(0.0, 0.0, 0.0, 1.0), surface.worldPosition, shadowMask),
        float3(0.0),
        float3(1.0)
    );

    output.color = float4(shadedColor, 1.0);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
