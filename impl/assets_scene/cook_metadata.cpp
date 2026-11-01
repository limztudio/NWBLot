// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/common/log.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;
using namespace SceneAssetCook;

[[nodiscard]] static Expected<SceneAsset::AssetMap> ParseAssetsMap(const Path& nwbFilePath, const Value& mapValue, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    if(!mapValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a map"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_SceneAssetsField));
        return MakeUnexpected(Failure{});
    }

    const auto& map = mapValue.asMap();
    SceneAsset::AssetMap assets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    assets.reserve(map.size());
    for(const auto& [nicknameKey, assetValue] : map){
        const AStringView nicknameView(nicknameKey.data(), nicknameKey.size());
        if(TrimView(nicknameView).empty() || nicknameView != TrimView(nicknameView)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset nickname '{}' must not be empty or padded"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(nicknameView));
            return MakeUnexpected(Failure{});
        }
        const auto nickname = Name::FromText(nicknameView);
        if(!nickname || !*nickname){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset nickname '{}' contains an embedded null"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(nicknameView));
            return MakeUnexpected(Failure{});
        }
        const auto parsedRef = Core::Assets::ParseMetadataReservedAssetRefValue<Core::Assets::IAsset>(nwbFilePath, s_SceneDiagnosticPrefix, nicknameView, assetValue, "entries must be strings", scratchArena);
        if(!parsedRef)
            return MakeUnexpected(Failure{});
        if(!assets.emplace(*nickname, *parsedRef).second){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': duplicate asset nickname '{}'"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(nicknameView));
            return MakeUnexpected(Failure{});
        }
    }
    return assets;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<EntityCookEntry> ParseEntityCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_scene_cook_metadata;

    EntityCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_EntityDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_EntityDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_EntityDiagnosticPrefix, { s_EntityComponentsField }))
        return MakeUnexpected(Failure{});

    const Core::Metascript::Value* componentsValue = asset.findField(s_EntityComponentsField);
    if(!componentsValue){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required"), ConvertString(s_EntityDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_EntityComponentsField));
        return MakeUnexpected(Failure{});
    }
    auto components = Core::Assets::ParseMetadataReservedAssetRefList<IComponent>(nwbFilePath, s_EntityDiagnosticPrefix, *componentsValue, s_EntityComponentsField, arena, scratchArena);
    if(!components)
        return MakeUnexpected(Failure{});

    EntityAsset candidate(arena, entry.virtualPath);
    candidate.setComponents(Move(*components));
    if(!candidate.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    entry.components = candidate.takeComponents();

    return entry;
}

Expected<EntityAsset> BuildEntityAsset(EntityCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    EntityAsset asset(arena, entry.virtualPath);
    asset.setComponents(Move(entry.components));
    if(!asset.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ProjectCookEntry> ParseProjectCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_scene_cook_metadata;

    ProjectCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_ProjectDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_ProjectDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_ProjectDiagnosticPrefix, { s_SceneStartSceneField }))
        return MakeUnexpected(Failure{});

    const auto startSceneField = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_ProjectDiagnosticPrefix, s_SceneStartSceneField, true);
    if(!startSceneField)
        return MakeUnexpected(Failure{});
    const auto startSceneRef = Core::Assets::ParseMetadataReservedAssetRefText<SceneAsset>(nwbFilePath, s_ProjectDiagnosticPrefix, s_SceneStartSceneField, startSceneField->text, scratchArena);
    if(!startSceneRef)
        return MakeUnexpected(Failure{});
    entry.startScene = *startSceneRef;

    ProjectAsset candidate(arena, entry.virtualPath);
    candidate.setStartScene(entry.startScene);
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});

    return entry;
}

Expected<ProjectAsset> BuildProjectAsset(ProjectCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
    ProjectAsset asset(arena, entry.virtualPath);
    asset.setStartScene(entry.startScene);
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SceneCookEntry> ParseSceneCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_scene_cook_metadata;

    SceneCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_SceneDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_SceneDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_SceneDiagnosticPrefix, { s_SceneSubsystemsField, s_SceneEntitiesField, s_SceneAssetsField }))
        return MakeUnexpected(Failure{});

    const Core::Metascript::Value* subsystemsValue = asset.findField(s_SceneSubsystemsField);
    if(!subsystemsValue){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_SceneSubsystemsField));
        return MakeUnexpected(Failure{});
    }
    auto subsystems = Core::Assets::ParseMetadataReservedAssetRefList<ISubsystem>(nwbFilePath, s_SceneDiagnosticPrefix, *subsystemsValue, s_SceneSubsystemsField, arena, scratchArena);
    if(!subsystems)
        return MakeUnexpected(Failure{});
    SubsystemAssetRuntime::DeduplicateSubsystemRefs(*subsystems);
    entry.subsystems = Move(*subsystems);

    if(const Core::Metascript::Value* entitiesValue = asset.findField(s_SceneEntitiesField)){
        auto entities = Core::Assets::ParseMetadataReservedAssetRefList<EntityAsset>(nwbFilePath, s_SceneDiagnosticPrefix, *entitiesValue, s_SceneEntitiesField, arena, scratchArena);
        if(!entities)
            return MakeUnexpected(Failure{});
        entry.entities = Move(*entities);
    }

    if(const Core::Metascript::Value* assetsValue = asset.findField(s_SceneAssetsField)){
        auto assets = ParseAssetsMap(nwbFilePath, *assetsValue, arena, scratchArena);
        if(!assets)
            return MakeUnexpected(Failure{});
        entry.assets = Move(*assets);
    }

    SceneAsset candidate(arena, entry.virtualPath);
    candidate.setContents(Move(entry.subsystems), Move(entry.assets), Move(entry.entities));
    if(!candidate.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    candidate.takeContents(entry.subsystems, entry.assets, entry.entities);

    return entry;
}

Expected<SceneAsset> BuildSceneAsset(SceneCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    SceneAsset asset(arena, entry.virtualPath);
    asset.setContents(Move(entry.subsystems), Move(entry.assets), Move(entry.entities));
    if(!asset.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

