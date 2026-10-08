// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/graphics/rhi/shader.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SHADER_ASSET_TYPE_ENTRIES(Entry) \
    Entry(VertexShader, VertexStage, "vertex_shader") \
    Entry(HullShader, HullStage, "hull_shader") \
    Entry(DomainShader, DomainStage, "domain_shader") \
    Entry(GeometryShader, GeometryStage, "geometry_shader") \
    Entry(PixelShader, PixelStage, "pixel_shader") \
    Entry(ComputeShader, ComputeStage, "compute_shader") \
    Entry(AmplificationShader, AmplificationStage, "amplification_shader") \
    Entry(MeshShader, MeshStage, "mesh_shader") \
    Entry(RayGenerationShader, RayGenerationStage, "ray_generation_shader") \
    Entry(AnyHitShader, AnyHitStage, "any_hit_shader") \
    Entry(ClosestHitShader, ClosestHitStage, "closest_hit_shader") \
    Entry(MissShader, MissStage, "miss_shader") \
    Entry(IntersectionShader, IntersectionStage, "intersection_shader") \
    Entry(CallableShader, CallableStage, "callable_shader")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderAssetTypes{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SHADER_ASSET_TYPE_NAME(Type, Stage, Text) inline constexpr Name s_##Type##Name(Text);
NWB_SHADER_ASSET_TYPE_ENTRIES(NWB_SHADER_ASSET_TYPE_NAME)
#undef NWB_SHADER_ASSET_TYPE_NAME

[[nodiscard]] inline constexpr Core::ShaderType::Enum ShaderTypeFromAssetType(const Name& assetType)noexcept{
#define NWB_SHADER_ASSET_TYPE_MATCH(Type, Stage, Text) if(assetType == s_##Type##Name) return Core::ShaderType::Stage;
    NWB_SHADER_ASSET_TYPE_ENTRIES(NWB_SHADER_ASSET_TYPE_MATCH)
#undef NWB_SHADER_ASSET_TYPE_MATCH
    return Core::ShaderType::Invalid;
}

[[nodiscard]] inline constexpr Core::ShaderType::Enum ShaderTypeFromAssetTypeText(const AStringView assetType)noexcept{
#define NWB_SHADER_ASSET_TYPE_TEXT_MATCH(Type, Stage, Text) if(assetType == AStringView(Text)) return Core::ShaderType::Stage;
    NWB_SHADER_ASSET_TYPE_ENTRIES(NWB_SHADER_ASSET_TYPE_TEXT_MATCH)
#undef NWB_SHADER_ASSET_TYPE_TEXT_MATCH
    return Core::ShaderType::Invalid;
}

[[nodiscard]] inline constexpr const Name& AssetTypeNameFromShaderType(const Core::ShaderType::Enum shaderType)noexcept{
    switch(shaderType){
#define NWB_SHADER_ASSET_TYPE_CASE(Type, Stage, Text) case Core::ShaderType::Stage: return s_##Type##Name;
        NWB_SHADER_ASSET_TYPE_ENTRIES(NWB_SHADER_ASSET_TYPE_CASE)
#undef NWB_SHADER_ASSET_TYPE_CASE
        default: return s_NameNone;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

