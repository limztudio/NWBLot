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
// Optional `.surface` fragment; transparent/refractive materials must use it.
static constexpr AStringView s_SurfaceField = "surface";
// Optional stage map; opaque non-refractive materials only.
static constexpr AStringView s_ShadersField = "shaders";
static constexpr AStringView s_ShaderVariantField = "shader_variant";
static constexpr AStringView s_ParametersField = "parameters";
// Required flags; explicit zeroes reject older shapes.
static constexpr AStringView s_TransparentField = "transparent";
static constexpr AStringView s_TwoSidedField = "two_sided";
// Separate from `transparent`, as a bare 0/1 flag.
static constexpr AStringView s_RefractiveField = "refractive";
// Required `.bxdf` path; the cook bakes the shading-model id.
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

