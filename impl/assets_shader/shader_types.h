// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SHADER_ASSET_TYPE_ENTRIES(Entry) \
    Entry(VertexShader) \
    Entry(HullShader) \
    Entry(DomainShader) \
    Entry(GeometryShader) \
    Entry(PixelShader) \
    Entry(ComputeShader) \
    Entry(AmplificationShader) \
    Entry(MeshShader) \
    Entry(RayGenerationShader) \
    Entry(AnyHitShader) \
    Entry(ClosestHitShader) \
    Entry(MissShader) \
    Entry(IntersectionShader) \
    Entry(CallableShader)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderAssetTypes{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline constexpr Core::ShaderType::Enum ShaderTypeFromAssetType(const Name& assetType)noexcept{
#define NWB_SHADER_ASSET_TYPE_MATCH(Type) if(assetType == Type::AssetTypeName()) return Type::s_Stage;
    NWB_SHADER_ASSET_TYPE_ENTRIES(NWB_SHADER_ASSET_TYPE_MATCH)
#undef NWB_SHADER_ASSET_TYPE_MATCH
    return Core::ShaderType::Invalid;
}

[[nodiscard]] inline constexpr Core::ShaderType::Enum ShaderTypeFromAssetTypeText(const AStringView assetType)noexcept{
#define NWB_SHADER_ASSET_TYPE_TEXT_MATCH(Type) if(assetType == Type::s_AssetTypeText) return Type::s_Stage;
    NWB_SHADER_ASSET_TYPE_ENTRIES(NWB_SHADER_ASSET_TYPE_TEXT_MATCH)
#undef NWB_SHADER_ASSET_TYPE_TEXT_MATCH
    return Core::ShaderType::Invalid;
}

[[nodiscard]] inline constexpr const Name& AssetTypeNameFromShaderType(const Core::ShaderType::Enum shaderType)noexcept{
    switch(shaderType){
#define NWB_SHADER_ASSET_TYPE_CASE(Type) case Type::s_Stage: return Type::AssetTypeName();
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
