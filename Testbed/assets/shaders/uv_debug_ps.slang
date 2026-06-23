// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_color.slangi"
#include "mesh/gbuffer_io.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbProjectUvDebug(const float2 uv0){
    const float2 wrappedUv = frac(uv0);
    const float2 checkerCell = floor(wrappedUv * 8.0);
    const float checker = fmod(checkerCell.x + checkerCell.y, 2.0);
    const float3 uvColor = float3(wrappedUv, 1.0 - wrappedUv.x);
    return lerp(uvColor * 0.55, uvColor, checker);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshGBufferOutput main(NwbMeshPixelInput input){
    nwbLoadMeshPixelInput(input);

    const float3 baseColor = nwbProjectUvDebug(inUv0) * inColor.rgb;
    // The shading model is data-driven: this material's `bxdf` (lighting bxdf) declaration determines which BXDF the deferred
    // lighting pass invokes for these pixels. The PS just writes the surface; the renderer delivers the id.
    nwbWriteMeshGBuffer(baseColor, inNormal, inWorldPosition);

    return nwbBuildMeshGBufferOutput();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

