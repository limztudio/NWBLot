// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/assets/asset_manager.h>
#include <core/graphics/graphics.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets_graphics/shader_asset.h>
#include <impl/assets_graphics/shader_stage_names.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderAssetLoader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ShaderPathResolver>
[[nodiscard]] bool EnsureLoaded(
    Core::ShaderHandle& outShader,
    const Name& shaderName,
    AStringView variantName,
    Core::ShaderType::Mask shaderType,
    const Name& debugName,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolver& shaderPathResolver,
    const tchar* ownerName,
    const Name* archiveStageName = nullptr
){
    if(outShader)
        return true;
    if(!shaderName){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: shader name is empty"), ownerName);
        return false;
    }

    const Name& stageName = archiveStageName
        ? *archiveStageName
        : ShaderStageNames::ArchiveStageNameFromShaderType(shaderType)
    ;
    if(!stageName){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: unsupported shader stage {}"), ownerName, static_cast<u32>(shaderType));
        return false;
    }

    const AStringView resolvedVariantName = variantName.empty()
        ? AStringView(Core::ShaderArchive::s_DefaultVariant)
        : variantName
    ;
    if(!shaderPathResolver){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: shader path resolver is null"), ownerName);
        return false;
    }

    Name shaderVirtualPath = NAME_NONE;
    if(!shaderPathResolver(shaderName, resolvedVariantName, stageName, shaderVirtualPath)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{}: failed to resolve shader '{}' variant '{}' stage '{}'"),
            ownerName,
            StringConvert(shaderName.c_str()),
            StringConvert(resolvedVariantName),
            StringConvert(stageName.c_str())
        );
        return false;
    }
    if(!shaderVirtualPath){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{}: shader resolver returned an empty path for shader '{}' variant '{}' stage '{}'"),
            ownerName,
            StringConvert(shaderName.c_str()),
            StringConvert(resolvedVariantName),
            StringConvert(stageName.c_str())
        );
        return false;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!assetManager.loadSync(Shader::AssetTypeName(), shaderVirtualPath, loadedAsset)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{}: failed to load shader '{}'"),
            ownerName,
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }
    if(!loadedAsset){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{}: shader asset '{}' is null"),
            ownerName,
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }
    if(loadedAsset->assetType() != Shader::AssetTypeName()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{}: asset '{}' is not a shader"),
            ownerName,
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }

    const Shader& shaderAsset = static_cast<const Shader&>(*loadedAsset);
    const Vector<u8>& shaderBinary = shaderAsset.bytecode();
    if(shaderBinary.empty() || (shaderBinary.size() & 3u) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{}: shader '{}' has invalid bytecode"),
            ownerName,
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }

    Core::ShaderDesc shaderDesc;
    shaderDesc.setShaderType(shaderType);
    shaderDesc.setDebugName(debugName);

    Core::IDevice* device = graphics.getDevice();
    outShader = device->createShader(shaderDesc, shaderBinary.data(), shaderBinary.size());
    if(!outShader){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{}: failed to create shader '{}' from asset '{}'"),
            ownerName,
            StringConvert(debugName.c_str()),
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

