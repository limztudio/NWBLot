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


static constexpr AStringView s_EnabledImplicitDefineValue = "1";
static constexpr AStringView s_IntervalSampleEnabledImplicitDefineValue = "1";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void InsertShaderKey(ShaderStageKeySet& outShaderKeys, const Name& shaderName, const Name& stageName){
    if(!shaderName || !stageName)
        return;
    const AssetsGraphicsCookDetail::ShaderStageKey key{ shaderName, stageName };
    if(outShaderKeys.contains(key))
        return;
    if(!outShaderKeys.insert(key).second){
        NWB_ASSERT(false);
        return;
    }
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

ShaderStageKeySet CollectMaterialClipShaderKeys(const ShaderCook::CookVector<MaterialCookEntry>& materialEntries, Core::Alloc::ScratchArena& scratchArena){
    ShaderStageKeySet shaderKeys{ 0, AssetsGraphicsCookDetail::ShaderStageKeyHasher(), EqualTo<AssetsGraphicsCookDetail::ShaderStageKey>(), scratchArena };
    shaderKeys.reserve(materialEntries.size());

    const Name& pixelStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::PixelStage);
    for(const MaterialCookEntry& materialEntry : materialEntries){
        const auto foundShader = materialEntry.stageShaders.find(Core::ShaderType::PixelStage);
        if(foundShader != materialEntry.stageShaders.end())
            InsertShaderKey(shaderKeys, foundShader.value().name(), pixelStageName);
    }

    CollectMaterialMeshShaderKeys(materialEntries, shaderKeys);
    return shaderKeys;
}

Expected<ShaderStageKeySet, NameTextError::Enum> CollectAvboitClipShaderKeys(const ShaderCook::CookVector<MaterialCookEntry>& materialEntries, Core::Alloc::ScratchArena& scratchArena){
    ShaderStageKeySet shaderKeys{ 0, AssetsGraphicsCookDetail::ShaderStageKeyHasher(), EqualTo<AssetsGraphicsCookDetail::ShaderStageKey>(), scratchArena };
    shaderKeys.reserve(materialEntries.size());

    CollectMaterialMeshShaderKeys(materialEntries, shaderKeys);

    const Name& pixelStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::PixelStage);
    // Transparent materials require CSG clip variants for each cook-generated AVBOIT pixel shader (accumulate, occupancy, extinction).
    for(const MaterialCookEntry& materialEntry : materialEntries){
        if(!materialEntry.avboitAccumulatePixelShaderName.empty()){
            const auto shaderName = Name::FromText(AStringView(materialEntry.avboitAccumulatePixelShaderName));
            if(!shaderName)
                return MakeUnexpected(shaderName.error());
            InsertShaderKey(shaderKeys, *shaderName, pixelStageName);
        }
        if(!materialEntry.avboitOccupancyPixelShaderName.empty()){
            const auto shaderName = Name::FromText(AStringView(materialEntry.avboitOccupancyPixelShaderName));
            if(!shaderName)
                return MakeUnexpected(shaderName.error());
            InsertShaderKey(shaderKeys, *shaderName, pixelStageName);
        }
        if(!materialEntry.avboitExtinctionPixelShaderName.empty()){
            const auto shaderName = Name::FromText(AStringView(materialEntry.avboitExtinctionPixelShaderName));
            if(!shaderName)
                return MakeUnexpected(shaderName.error());
            InsertShaderKey(shaderKeys, *shaderName, pixelStageName);
        }
    }
    return shaderKeys;
}

Expected<bool, NameTextError::Enum> SupportsClipVariant(const ShaderStageKeySet& shaderKeys, const ShaderCook::ShaderEntry& shaderEntry){
    const auto shaderName = ToName(shaderEntry.name);
    const auto stageName = ToName(shaderEntry.archiveStage);
    if(!shaderName)
        return MakeUnexpected(shaderName.error());
    if(!stageName)
        return MakeUnexpected(stageName.error());
    const AssetsGraphicsCookDetail::ShaderStageKey shaderKey{ *shaderName ? **shaderName : s_NameNone, *stageName ? **stageName : s_NameNone };
    return shaderKeys.find(shaderKey) != shaderKeys.end();
}

bool AddClipVariantCount(const ShaderCook::ShaderEntry& entry, const u64 sourceVariantCount, u64& inOutVariantCount){
    if(sourceVariantCount <= Limit<u64>::s_Max - inOutVariantCount){
        inOutVariantCount += sourceVariantCount;
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: CSG clip variant count overflow for entry '{}'"), StringConvert(entry.name));
    return false;
}

Expected<ShaderCook::DefineCombo> BuildClipDefineCombo(ShaderCook::CookArena& cookArena, const AStringView entryName, const ShaderCook::DefineCombo& sourceCombo){
    constexpr usize s_AddedDefineCount = 2u;
    if(sourceCombo.size() > Limit<usize>::s_Max - s_AddedDefineCount){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: CSG shader define combo size overflow for entry '{}'"), StringConvert(entryName));
        return MakeUnexpected(Failure{});
    }

    ShaderCook::DefineCombo combo{cookArena};
    combo.reserve(sourceCombo.size() + s_AddedDefineCount);
    for(const auto& [defineName, defineValue] : sourceCombo){
        if(!combo.try_emplace(defineName, defineValue).second){
            NWB_ASSERT(false);
            return MakeUnexpected(Failure{});
        }
    }

    if(!combo.try_emplace(s_IntervalSampleEnabledImplicitDefineName, s_IntervalSampleEnabledImplicitDefineValue).second){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: reserved CSG shader define '{}' already exists for entry '{}'"), StringConvert(s_IntervalSampleEnabledImplicitDefineName), StringConvert(entryName));
        return MakeUnexpected(Failure{});
    }

    if(combo.try_emplace(s_ClipImplicitDefineName, s_EnabledImplicitDefineValue).second)
        return combo;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: reserved CSG shader define '{}' already exists for entry '{}'"), StringConvert(s_ClipImplicitDefineName), StringConvert(entryName));
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

