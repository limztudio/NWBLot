// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_shader_variants.h"

#include <impl/assets_shader/asset.h>

#include <core/graphics/shader_stage_names.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCsgShaderVariants{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_ClipImplicitDefineName = "NWB_CSG_ENABLED";
static constexpr AStringView s_EnabledImplicitDefineValue = "1";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AStringView ClipImplicitDefineName(){
    return s_ClipImplicitDefineName;
}

void CollectMaterialClipShaderKeys(const ShaderCook::CookVector<MaterialCookEntry>& materialEntries, ShaderStageKeySet& outShaderKeys){
    outShaderKeys.clear();
    outShaderKeys.reserve(materialEntries.size());

    const Name& pixelStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::PixelStage);
    const Name& meshStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::MeshStage);
    for(const MaterialCookEntry& materialEntry : materialEntries){
        const auto foundShader = materialEntry.stageShaders.find(Core::ShaderType::PixelStage);
        if(foundShader != materialEntry.stageShaders.end()){
            const Core::Assets::AssetRef<Shader>& shaderAsset = foundShader.value();
            if(shaderAsset.name())
                outShaderKeys.insert(AssetsGraphicsCookDetail::ShaderStageKey{ shaderAsset.name(), pixelStageName });
        }

        const auto foundMeshShader = materialEntry.stageShaders.find(Core::ShaderType::MeshStage);
        if(foundMeshShader != materialEntry.stageShaders.end()){
            const Core::Assets::AssetRef<Shader>& shaderAsset = foundMeshShader.value();
            if(shaderAsset.name())
                outShaderKeys.insert(AssetsGraphicsCookDetail::ShaderStageKey{ shaderAsset.name(), meshStageName });
        }
    }
}

bool SupportsClipVariant(const ShaderStageKeySet& shaderKeys, const ShaderCook::ShaderEntry& shaderEntry){
    const AssetsGraphicsCookDetail::ShaderStageKey shaderKey{
        ToName(shaderEntry.name),
        ToName(shaderEntry.archiveStage.view())
    };
    return shaderKeys.find(shaderKey) != shaderKeys.end();
}

bool AddClipVariantCount(const ShaderCook::ShaderEntry& entry, u64& inOutVariantCount){
    if(inOutVariantCount <= Limit<u64>::s_Max / 2ull){
        inOutVariantCount *= 2ull;
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: CSG clip variant count overflow for entry '{}'"), StringConvert(entry.name));
    return false;
}

bool BuildClipDefineCombo(
    ShaderCook::CookArena& cookArena,
    const AStringView entryName,
    const ShaderCook::DefineCombo& sourceCombo,
    ShaderCook::DefineCombo& outDefineCombo
){
    if(sourceCombo.size() == Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: CSG shader define combo size overflow for entry '{}'"), StringConvert(entryName));
        return false;
    }

    outDefineCombo.clear();
    outDefineCombo.reserve(sourceCombo.size() + 1u);
    for(const auto& [defineName, defineValue] : sourceCombo)
        outDefineCombo.try_emplace(defineName, defineValue);

    ShaderCook::CookString csgDefineName(s_ClipImplicitDefineName, cookArena);
    ShaderCook::CookString csgDefineValue(s_EnabledImplicitDefineValue, cookArena);
    if(outDefineCombo.try_emplace(Move(csgDefineName), Move(csgDefineValue)).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: reserved CSG shader define '{}' already exists for entry '{}'")
        , StringConvert(s_ClipImplicitDefineName)
        , StringConvert(entryName)
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

