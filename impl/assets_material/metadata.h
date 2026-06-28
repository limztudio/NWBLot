// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <global/basic_string.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialAssetMetadataSchema{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_InterfaceField = "interface";
// Optional. A `project/`-rooted `.surface` fragment that defines this material's nwbMaterialSurface()
// hook (the per-pixel G-buffer surface: base color + normal + BXDF params). When present (and `shaders` is
// absent), the cook generates this material's G-buffer pixel shader by wrapping the fragment with the engine
// pixel-shader authoring + the material's typed `.bind`, and points the material's mesh stage at the shared
// engine mesh shader. A material declares either `surface` (cook generates its shaders) or `shaders` (explicit),
// not both.
static constexpr AStringView s_SurfaceField = "surface";
// Optional. Explicit stage->project-shader virtual-name map. When omitted, the cook generates the pixel shader from
// `surface` and assigns the shared engine mesh shader.
static constexpr AStringView s_ShadersField = "shaders";
static constexpr AStringView s_ShaderVariantField = "shader_variant";
static constexpr AStringView s_ParametersField = "parameters";
static constexpr AStringView s_TransparentField = "transparent";
static constexpr AStringView s_TwoSidedField = "two_sided";
// The dedicated refractive-caster classification flag (SEPARATE from `transparent`). Authored exactly like
// `transparent`/`two_sided` as a bare 0/1 flag (`asset.refractive = 1;`). The material decides ONLY this boolean;
// the actual refraction VALUES (ior/thickness/transmission) are shader-side -- the `.surface` hook returns them
// via NwbMeshSurface -- mirroring how the colored-shadow transmittance is shader-decided. Default 0 (not a
// refractive caster), so a material declaring none behaves identically to today.
static constexpr AStringView s_RefractiveField = "refractive";
// Required (at cook): a `project/`-rooted virtual path with the dedicated `.bxdf` extension (e.g.
// "project/shaders/lambert.bxdf") authoring the deferred lighting BXDF for this material's surfaces. The cook
// resolves it, assigns each unique BXDF a shading-model id, generates the deferred lighting dispatch module from
// them, and bakes this material's id into its cooked asset. The engine never ships a default BXDF.
static constexpr AStringView s_BxdfField = "bxdf";

static constexpr AStringView s_AllowedAssetFields[] = {
    s_InterfaceField,
    s_SurfaceField,
    s_ShadersField,
    s_ShaderVariantField,
    s_ParametersField,
    s_TransparentField,
    s_TwoSidedField,
    s_RefractiveField,
    s_BxdfField,
};

[[nodiscard]] inline bool IsAllowedAssetField(const AStringView fieldName){
    for(const AStringView allowedField : s_AllowedAssetFields){
        if(fieldName == allowedField)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

