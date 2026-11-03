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


static void InsertShaderKey(const Name& shaderName, const Name& stageName, ShaderStageKeySet& inOutShaderKeys){
    if(!shaderName || !stageName)
        return;
    const ShaderStageKey key(shaderName, stageName);
    if(inOutShaderKeys.contains(key))
        return;
    if(!inOutShaderKeys.insert(key).second){
        NWB_ASSERT(false);
        return;
    }
}

static void CollectMaterialMeshShaderKeys(const Span<const MaterialCookEntry> materialEntries, ShaderStageKeySet& inOutShaderKeys){
    const Name& meshStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::MeshStage);
    for(const MaterialCookEntry& materialEntry : materialEntries){
        const auto foundMeshShader = materialEntry.stageShaders.find(Core::ShaderType::MeshStage);
        if(foundMeshShader == materialEntry.stageShaders.end())
            continue;

        InsertShaderKey(foundMeshShader.value().name(), meshStageName, inOutShaderKeys);
    }
}

ShaderStageKeySet CollectMaterialClipShaderKeys(const Span<const MaterialCookEntry> materialEntries, Core::Alloc::ScratchArena& scratchArena){
    ShaderStageKeySet shaderKeys{ 0, Hasher<ShaderStageKey>(), EqualTo<ShaderStageKey>(), scratchArena };
    shaderKeys.reserve(materialEntries.size());

    const Name& pixelStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::PixelStage);
    for(const MaterialCookEntry& materialEntry : materialEntries){
        const auto foundShader = materialEntry.stageShaders.find(Core::ShaderType::PixelStage);
        if(foundShader != materialEntry.stageShaders.end())
            InsertShaderKey(foundShader.value().name(), pixelStageName, shaderKeys);
    }

    CollectMaterialMeshShaderKeys(materialEntries, shaderKeys);
    return shaderKeys;
}

Expected<ShaderStageKeySet, NameTextError::Enum> CollectAvboitClipShaderKeys(const Span<const MaterialCookEntry> materialEntries, Core::Alloc::ScratchArena& scratchArena){
    ShaderStageKeySet shaderKeys{ 0, Hasher<ShaderStageKey>(), EqualTo<ShaderStageKey>(), scratchArena };
    shaderKeys.reserve(materialEntries.size());

    CollectMaterialMeshShaderKeys(materialEntries, shaderKeys);

    const Name& pixelStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::PixelStage);
    // Transparent materials require CSG clip variants for each cook-generated AVBOIT pixel shader (accumulate, occupancy, extinction).
    for(const MaterialCookEntry& materialEntry : materialEntries){
        if(!materialEntry.avboitAccumulatePixelShaderName.empty()){
            const auto shaderName = Name::FromText(AStringView(materialEntry.avboitAccumulatePixelShaderName));
            if(!shaderName)
                return MakeUnexpected(shaderName.error());
            InsertShaderKey(*shaderName, pixelStageName, shaderKeys);
        }
        if(!materialEntry.avboitOccupancyPixelShaderName.empty()){
            const auto shaderName = Name::FromText(AStringView(materialEntry.avboitOccupancyPixelShaderName));
            if(!shaderName)
                return MakeUnexpected(shaderName.error());
            InsertShaderKey(*shaderName, pixelStageName, shaderKeys);
        }
        if(!materialEntry.avboitExtinctionPixelShaderName.empty()){
            const auto shaderName = Name::FromText(AStringView(materialEntry.avboitExtinctionPixelShaderName));
            if(!shaderName)
                return MakeUnexpected(shaderName.error());
            InsertShaderKey(*shaderName, pixelStageName, shaderKeys);
        }
    }
    return shaderKeys;
}

Expected<b8, NameTextError::Enum> SupportsClipVariant(const ShaderStageKeySet& shaderKeys, const ShaderCook::ShaderEntry& shaderEntry){
    const auto shaderName = ToName(shaderEntry.name);
    const auto stageName = ToName(shaderEntry.archiveStage);
    if(!shaderName)
        return MakeUnexpected(shaderName.error());
    if(!stageName)
        return MakeUnexpected(stageName.error());
    const ShaderStageKey shaderKey(*shaderName ? **shaderName : s_NameNone, *stageName ? **stageName : s_NameNone);
    return shaderKeys.find(shaderKey) != shaderKeys.end();
}

b8 AddClipVariantCount(const ShaderCook::ShaderEntry& entry, const u64 sourceVariantCount, u64& inOutVariantCount){
    return TryAddCsgClipVariantCount(entry.name, sourceVariantCount, inOutVariantCount);
}

Expected<ShaderCook::DefineCombo> BuildClipDefineCombo(const AStringView entryName, const ShaderCook::DefineCombo& sourceCombo, ShaderCook::CookArena& cookArena){
    if(sourceCombo.size() > Limit<usize>::s_Max - s_CsgClipAddedDefineCount){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: CSG shader define combo size overflow for entry '{}'"), ConvertString(entryName));
        return MakeUnexpected(Failure{});
    }

    ShaderCook::DefineCombo combo{cookArena};
    combo.reserve(sourceCombo.size() + s_CsgClipAddedDefineCount);
    for(const auto& [defineName, defineValue] : sourceCombo){
        if(!combo.try_emplace(defineName, defineValue).second){
            NWB_ASSERT(false);
            return MakeUnexpected(Failure{});
        }
    }

    if(!combo.try_emplace(s_CsgIntervalSampleEnabledImplicitDefineName, s_IntervalSampleEnabledImplicitDefineValue).second){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: reserved CSG shader define '{}' already exists for entry '{}'"), ConvertString(s_CsgIntervalSampleEnabledImplicitDefineName), ConvertString(entryName));
        return MakeUnexpected(Failure{});
    }

    if(combo.try_emplace(s_CsgClipImplicitDefineName, s_EnabledImplicitDefineValue).second)
        return combo;

    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: reserved CSG shader define '{}' already exists for entry '{}'"), ConvertString(s_CsgClipImplicitDefineName), ConvertString(entryName));
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

