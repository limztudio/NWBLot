
#include "lighting_framework.slangi"
// Cook-generated dispatch module: assembled from every material's `bxdf` declaration (see the asset cooker's
// EmitDeferredBxdfDispatchModule). It defines nwbDeferredDispatchBxdf(shadingModel, surface, pixel),
// which routes each pixel to the BXDF the owning material authored. The engine ships no BXDF of its own.
#include "deferred/generated/bxdf_dispatch.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deferred lighting harness. The engine provides only the frame: unpack the surface the geometry pass packed
// into the G-buffer, pass through the background, then invoke the material-authored BXDF by its shading-model
// id. All shading lives in the per-material BXDFs the cook assembled into the dispatch module above; nothing
// here hard-codes a model, and there is no default (an unknown id renders magenta).
NwbDeferredLightingPixelOutput main(NwbDeferredLightingPixelInput input){
    NwbDeferredLightingPixelOutput output;

    const NwbBxdfSurface surface = nwbBxdfLoadSurface(input.uv);
    if(surface.isBackground){
        output.color = half4(surface.baseColor, half(1.0));
        return output;
    }

    const int2 pixel = int2(input.position.xy);
    const half3 shadedColor = clamp(
        nwbDeferredDispatchBxdf(surface.shadingModel, surface, pixel),
        half3(0.0),
        half3(1.0)
    );

    output.color = half4(shadedColor, half(1.0));
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

