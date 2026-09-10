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
// Optional `project/`-rooted `.surface` fragment defining nwbMaterialSurface().
// The cook wraps it with the engine pixel authoring + typed `.bind`. Transparent or
// refractive materials must use it; declares either `surface` or `shaders`, not both.
static constexpr AStringView s_SurfaceField = "surface";
// Optional explicit stage->shader map. Only for opaque, non-refractive materials.
static constexpr AStringView s_ShadersField = "shaders";
static constexpr AStringView s_ShaderVariantField = "shader_variant";
static constexpr AStringView s_ParametersField = "parameters";
// Required flags. Explicit zeroes keep older metadata shapes rejected.
static constexpr AStringView s_TransparentField = "transparent";
static constexpr AStringView s_TwoSidedField = "two_sided";
// Refractive-caster flag (separate from `transparent`), authored as a bare 0/1 flag.
// Refraction values stay shader-side via NwbMeshSurface.
static constexpr AStringView s_RefractiveField = "refractive";
// Required `project/`-rooted `.bxdf` path for deferred lighting. The cook assigns the
// shading-model id and bakes it into the cooked asset. No engine default BXDF.
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

