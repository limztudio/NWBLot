// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_shader_variants.h"

#include <impl/assets/graphics/avboit/names.h>
#include <impl/assets_shader/asset.h>

#include <core/graphics/shader_stage_names.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCsgShaderVariants{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_EnabledImplicitDefineValue = "1";
static constexpr AStringView s_IntervalSampleEnabledImplicitDefineValue = "1";
static constexpr AStringView s_AvboitClipSetImplicitDefineValue = "2";
static constexpr AStringView s_AvboitIntervalSampleSetImplicitDefineValue = "3";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void InsertShaderKey(
    ShaderStageKeySet& outShaderKeys,
    const Name& shaderName,
    const Name& stageName
){
    if(shaderName && stageName)
        outShaderKeys.insert(AssetsGraphicsCookDetail::ShaderStageKey{ shaderName, stageName });
}

static void CollectMaterialMeshShaderKeys(const ShaderCook::CookVector<MaterialCookEntry>& materialEntries, ShaderStageKeySet& outShaderKeys){
    const Name& meshStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::MeshStage);
    for(const MaterialCookEntry& materialEntry : materialEntries){
        const auto foundMeshShader = materialEntry.stageShaders.find(Core::ShaderType::MeshStage);
        if(foundMeshShader == materialEntry.stageShaders.end())
            continue;

        InsertShaderKey(outShaderKeys, foundMeshShader.value().name(), meshStageName);
    }
}

void CollectMaterialClipShaderKeys(const ShaderCook::CookVector<MaterialCookEntry>& materialEntries, ShaderStageKeySet& outShaderKeys){
    outShaderKeys.clear();
    outShaderKeys.reserve(materialEntries.size());

    const Name& pixelStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::PixelStage);
    for(const MaterialCookEntry& materialEntry : materialEntries){
        const auto foundShader = materialEntry.stageShaders.find(Core::ShaderType::PixelStage);
        if(foundShader != materialEntry.stageShaders.end())
            InsertShaderKey(outShaderKeys, foundShader.value().name(), pixelStageName);
    }

    CollectMaterialMeshShaderKeys(materialEntries, outShaderKeys);
}

void CollectAvboitClipShaderKeys(const ShaderCook::CookVector<MaterialCookEntry>& materialEntries, ShaderStageKeySet& outShaderKeys){
    outShaderKeys.clear();
    outShaderKeys.reserve(materialEntries.size());

    CollectMaterialMeshShaderKeys(materialEntries, outShaderKeys);

    const Name& pixelStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::PixelStage);
    InsertShaderKey(outShaderKeys, AssetsGraphicsAvboit::s_OccupancyPixelShaderName, pixelStageName);
    InsertShaderKey(outShaderKeys, AssetsGraphicsAvboit::s_ExtinctionPixelShaderName, pixelStageName);
    InsertShaderKey(outShaderKeys, AssetsGraphicsAvboit::s_AccumulatePixelShaderName, pixelStageName);
}

bool SupportsClipVariant(const ShaderStageKeySet& shaderKeys, const ShaderCook::ShaderEntry& shaderEntry){
    const AssetsGraphicsCookDetail::ShaderStageKey shaderKey{
        ToName(shaderEntry.name),
        ToName(shaderEntry.archiveStage.view())
    };
    return shaderKeys.find(shaderKey) != shaderKeys.end();
}

bool AddClipVariantCount(const ShaderCook::ShaderEntry& entry, const u64 sourceVariantCount, u64& inOutVariantCount){
    if(sourceVariantCount <= Limit<u64>::s_Max - inOutVariantCount){
        inOutVariantCount += sourceVariantCount;
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: CSG clip variant count overflow for entry '{}'"), StringConvert(entry.name));
    return false;
}

static bool BuildClipDefineComboImpl(
    ShaderCook::CookArena& cookArena,
    const AStringView entryName,
    const ShaderCook::DefineCombo& sourceCombo,
    const bool avboitClipSet,
    ShaderCook::DefineCombo& outDefineCombo
){
    const usize addedDefineCount = avboitClipSet ? 3u : 2u;
    if(sourceCombo.size() > Limit<usize>::s_Max - addedDefineCount){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: CSG shader define combo size overflow for entry '{}'"), StringConvert(entryName));
        return false;
    }

    outDefineCombo.clear();
    outDefineCombo.reserve(sourceCombo.size() + addedDefineCount);
    for(const auto& [defineName, defineValue] : sourceCombo)
        outDefineCombo.try_emplace(defineName, defineValue);

    if(avboitClipSet){
        ShaderCook::CookString csgClipSetDefineName(s_ClipSetImplicitDefineName, cookArena);
        ShaderCook::CookString csgClipSetDefineValue(s_AvboitClipSetImplicitDefineValue, cookArena);
        if(!outDefineCombo.try_emplace(Move(csgClipSetDefineName), Move(csgClipSetDefineValue)).second){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: reserved CSG shader define '{}' already exists for entry '{}'")
                , StringConvert(s_ClipSetImplicitDefineName)
                , StringConvert(entryName)
            );
            return false;
        }

        ShaderCook::CookString csgIntervalSampleSetDefineName(s_IntervalSampleSetImplicitDefineName, cookArena);
        ShaderCook::CookString csgIntervalSampleSetDefineValue(s_AvboitIntervalSampleSetImplicitDefineValue, cookArena);
        if(!outDefineCombo.try_emplace(Move(csgIntervalSampleSetDefineName), Move(csgIntervalSampleSetDefineValue)).second){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: reserved CSG shader define '{}' already exists for entry '{}'")
                , StringConvert(s_IntervalSampleSetImplicitDefineName)
                , StringConvert(entryName)
            );
            return false;
        }
    }
    else{
        ShaderCook::CookString csgIntervalSampleEnabledDefineName(s_IntervalSampleEnabledImplicitDefineName, cookArena);
        ShaderCook::CookString csgIntervalSampleEnabledDefineValue(s_IntervalSampleEnabledImplicitDefineValue, cookArena);
        if(!outDefineCombo.try_emplace(Move(csgIntervalSampleEnabledDefineName), Move(csgIntervalSampleEnabledDefineValue)).second){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: reserved CSG shader define '{}' already exists for entry '{}'")
                , StringConvert(s_IntervalSampleEnabledImplicitDefineName)
                , StringConvert(entryName)
            );
            return false;
        }
    }

    ShaderCook::CookString csgDefineName(s_ClipImplicitDefineName, cookArena);
    ShaderCook::CookString csgDefineValue(s_EnabledImplicitDefineValue, cookArena);
    if(outDefineCombo.try_emplace(Move(csgDefineName), Move(csgDefineValue)).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: reserved CSG shader define '{}' already exists for entry '{}'")
        , StringConvert(s_ClipImplicitDefineName)
        , StringConvert(entryName)
    );
    return false;
}

bool BuildClipDefineCombo(
    ShaderCook::CookArena& cookArena,
    const AStringView entryName,
    const ShaderCook::DefineCombo& sourceCombo,
    ShaderCook::DefineCombo& outDefineCombo
){
    return BuildClipDefineComboImpl(cookArena, entryName, sourceCombo, false, outDefineCombo);
}

bool BuildAvboitClipDefineCombo(
    ShaderCook::CookArena& cookArena,
    const AStringView entryName,
    const ShaderCook::DefineCombo& sourceCombo,
    ShaderCook::DefineCombo& outDefineCombo
){
    return BuildClipDefineComboImpl(cookArena, entryName, sourceCombo, true, outDefineCombo);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

