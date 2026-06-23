// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh/material_ps_authoring.slangi"
#include "project/shaders/smoke_surface.bind"
#include "smoke_material_color.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Transparent smoke surface hook. Demonstrates a per-material color transform expressed as a hook (not a
// compile-time variant): the base color is square-root transformed and warm-tinted, read per-pixel from the
// typed material constants.
NwbMeshSurface nwbMaterialSurface(){
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const NwbSmokeSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    const NwbSmokeRuntimeMaterial runtime = nwbMaterialBindLoadRuntime(instance);
    const float3 baseColor = nwbSmokeMaterialVertexColor((surface.base_color * runtime.color_tint).rgb);
    return nwbMakeMeshSurface(baseColor, inNormal);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
