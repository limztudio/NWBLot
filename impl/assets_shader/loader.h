// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/assets/manager.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/shader_stage_names.h>
#include <impl/assets_shader/asset.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderAssetLoader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ShaderPathResolver>
[[nodiscard]] bool LoadForStage(
    Core::ShaderHandle& outShader,
    const Name& shaderName,
    AStringView variantName,
    Core::ShaderType::Mask shaderType,
    const Name& debugName,
    Core::GraphicsRuntime& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolver& shaderPathResolver,
    const TStringView ownerName,
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
        : Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType)
    ;
    if(!stageName){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: unsupported shader stage {}"), ownerName, static_cast<u32>(shaderType));
        return false;
    }

    if(variantName.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: shader variant is empty"), ownerName);
        return false;
    }
    if(!shaderPathResolver){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: shader path resolver is null"), ownerName);
        return false;
    }

    Name shaderVirtualPath = s_NameNone;
    if(!shaderPathResolver(shaderName, variantName, stageName, shaderVirtualPath)){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to resolve shader '{}' variant '{}' stage '{}'")
            , ownerName
            , StringConvert(shaderName.resolvedText())
            , StringConvert(variantName)
            , StringConvert(stageName.resolvedText())
        );
        return false;
    }
    if(!shaderVirtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: shader resolver returned an empty path for shader '{}' variant '{}' stage '{}'")
            , ownerName
            , StringConvert(shaderName.resolvedText())
            , StringConvert(variantName)
            , StringConvert(stageName.resolvedText())
        );
        return false;
    }

    const Core::ShaderType::Enum physicalStage = Core::ShaderType::ToEnum(shaderType);
    const Name& assetType = ShaderAssetTypes::AssetTypeNameFromShaderType(physicalStage);
    if(!assetType){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: unsupported physical shader stage {}"), ownerName, static_cast<u32>(shaderType));
        return false;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!assetManager.loadSync(assetType, shaderVirtualPath, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to load {} asset '{}'")
            , ownerName
            , StringConvert(assetType.resolvedText())
            , StringConvert(shaderVirtualPath.resolvedText())
        );
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != assetType){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: loaded shader asset has an unexpected concrete type"), ownerName);
        return false;
    }

    const IShader& shaderAsset = *checked_cast<const IShader*>(loadedAsset.get());
    const Core::Assets::AssetBytes& shaderBinary = shaderAsset.bytecode();
    NWB_ASSERT(!shaderAsset.entryPoint().empty() && !shaderBinary.empty() && (shaderBinary.size() & 3u) == 0u);

    Core::ShaderDesc shaderDesc;
    shaderDesc.setShaderType(shaderType);
    shaderDesc.setDebugName(debugName);
    shaderDesc.setEntryName(AStringView(shaderAsset.entryPoint()));

    auto& device = graphics.getDevice();
    outShader = device.createShader(shaderDesc, shaderBinary.data(), shaderBinary.size());
    if(!outShader){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create shader '{}' from asset '{}'")
            , ownerName
            , StringConvert(debugName.resolvedText())
            , StringConvert(shaderVirtualPath.resolvedText())
        );
        return false;
    }

    return true;
}


template<typename ShaderT, typename ShaderPathResolver>
    requires(IsBaseOf_V<IShader, ShaderT> && !IsAbstract_V<ShaderT>)
[[nodiscard]] bool Load(
    Core::ShaderHandle& outShader,
    const Name& shaderName,
    const AStringView variantName,
    const Name& debugName,
    Core::GraphicsRuntime& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolver& shaderPathResolver,
    const TStringView ownerName,
    const Name* archiveStageName = nullptr
){
    static_assert(Core::ShaderType::IsValid(ShaderT::s_Stage));
    return LoadForStage(
        outShader,
        shaderName,
        variantName,
        Core::ShaderType::ToMask(ShaderT::s_Stage),
        debugName,
        graphics,
        assetManager,
        shaderPathResolver,
        ownerName,
        archiveStageName
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

