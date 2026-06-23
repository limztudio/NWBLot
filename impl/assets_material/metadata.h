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
// Optional. A `project/`/`engine/`-rooted `.surface` fragment that defines this material's nwbMaterialSurface()
// hook (the per-pixel G-buffer surface: base color + normal + BXDF params). When present (and `shaders` is
// absent), the cook generates this material's G-buffer pixel shader by wrapping the fragment with the engine
// pixel-shader authoring + the material's typed `.bind`, and points the material's mesh stage at the shared
// engine mesh shader. A material declares either `surface` (cook generates its shaders) or `shaders` (explicit),
// not both.
static constexpr AStringView s_SurfaceField = "surface";
// Optional/legacy. Explicit stage->shader virtual-name map. When omitted, the cook generates the pixel shader
// from `surface` and assigns the shared engine mesh shader.
static constexpr AStringView s_ShadersField = "shaders";
static constexpr AStringView s_ShaderVariantField = "shader_variant";
static constexpr AStringView s_ParametersField = "parameters";
static constexpr AStringView s_TransparentField = "transparent";
static constexpr AStringView s_TwoSidedField = "two_sided";
// Required (at cook): a `project/`/`engine/`-rooted virtual path with the dedicated `.bxdf` extension (e.g.
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

