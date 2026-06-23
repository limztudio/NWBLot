// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh/material_ps_authoring.slangi"
#include "project/shaders/surface.bind"
#include "material_color.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This material's surface hook. The engine owns the pixel-shader entry + the G-buffer write; this only
// describes the surface: base color from the typed material constants (base_color x color_tint), read
// per-pixel. The shading model is data-driven from the material's `bxdf` declaration, delivered by the engine.
NwbMeshSurface nwbMaterialSurface(){
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const NwbSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    const NwbRuntimeMaterial runtime = nwbMaterialBindLoadRuntime(instance);
    const float3 baseColor = (surface.base_color * runtime.color_tint).rgb;
    return nwbMakeMeshSurface(nwbMaterialVertexColor(baseColor), inNormal);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
