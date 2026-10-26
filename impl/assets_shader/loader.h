// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/assets_shader/asset.h>
#include <impl/assets_shader/binary_payload.h>

#include <core/assets/manager.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/shader_stage_names.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderAssetLoader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    constexpr Core::ShaderType::Mask s_ShaderType = Core::ShaderType::ToMask(ShaderT::s_Stage);

    if(outShader)
        return true;
    if(!shaderName){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: shader name is empty"), ownerName);
        return false;
    }

    const Name& stageName = archiveStageName ? *archiveStageName : Core::ShaderStageNames::ArchiveStageNameFromShaderType(s_ShaderType);
    if(!stageName){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: unsupported shader stage {}"), ownerName, static_cast<u32>(s_ShaderType));
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

    const auto shaderVirtualPath = shaderPathResolver(shaderName, variantName, stageName);
    if(!shaderVirtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to resolve shader '{}' variant '{}' stage '{}'"), ownerName, ConvertString(shaderName.resolvedText()), ConvertString(variantName), ConvertString(stageName.resolvedText()));
        return false;
    }
    if(!*shaderVirtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: shader resolver returned an empty path for shader '{}' variant '{}' stage '{}'"), ownerName, ConvertString(shaderName.resolvedText()), ConvertString(variantName), ConvertString(stageName.resolvedText()));
        return false;
    }

    auto loadedAsset = assetManager.loadTypedSync<ShaderT>(*shaderVirtualPath, ownerName);
    if(!loadedAsset)
        return false;

    const ShaderT& shaderAsset = **loadedAsset;
    const Core::Assets::AssetBytes& shaderBinary = shaderAsset.bytecode();
    NWB_ASSERT(!shaderAsset.entryPoint().empty() && !shaderBinary.empty() && (shaderBinary.size() & ShaderBinaryPayload::s_SpvWordByteMask) == 0u);

    Core::ShaderDesc shaderDesc;
    shaderDesc.setShaderType(s_ShaderType);
    shaderDesc.setDebugName(debugName);
    shaderDesc.setEntryName(AStringView(shaderAsset.entryPoint()));

    auto& device = graphics.getDevice();
    auto outShaderResult = device.createShader(shaderDesc, shaderBinary.data(), shaderBinary.size());
    if(outShaderResult)
        outShader = Move(*outShaderResult);
    else
        outShader.reset();
    if(!outShaderResult){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create shader '{}' from asset '{}'"), ownerName, ConvertString(debugName.resolvedText()), ConvertString(shaderVirtualPath->resolvedText()));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

