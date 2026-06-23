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
static constexpr AStringView s_ShadersField = "shaders";
static constexpr AStringView s_ShaderVariantField = "shader_variant";
static constexpr AStringView s_ParametersField = "parameters";
static constexpr AStringView s_TransparentField = "transparent";
static constexpr AStringView s_TwoSidedField = "two_sided";
// Required: a .slangi include (relative to this material's asset root) authoring the deferred lighting BXDF
// for this material's surfaces. The cook assigns each unique BXDF a shading-model id, generates the deferred
// lighting dispatch module from them, and bakes this material's id into its cooked asset. The engine never
// ships a default BXDF: every material declares its own.
static constexpr AStringView s_BxdfField = "bxdf";

static constexpr AStringView s_AllowedAssetFields[] = {
    s_InterfaceField,
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

