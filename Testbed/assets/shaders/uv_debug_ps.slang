// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_color.slangi"
#include "mesh/gbuffer_ps.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbProjectUvDebug(const float2 uv0){
    const float2 wrappedUv = frac(uv0);
    const float2 checkerCell = floor(wrappedUv * 8.0);
    const float checker = fmod(checkerCell.x + checkerCell.y, 2.0);
    const float3 uvColor = float3(wrappedUv, 1.0 - wrappedUv.x);
    return lerp(uvColor * 0.55, uvColor, checker);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This material's surface hook. The engine owns the pixel-shader entry + the G-buffer write; this only
// describes the surface. The shading model is data-driven from the material's `bxdf` (lighting) declaration,
// delivered by the engine -- it is not chosen here.
NwbMeshSurface nwbMaterialSurface(){
    return nwbMakeMeshSurface(nwbProjectUvDebug(inUv0) * inColor.rgb, inNormal);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

