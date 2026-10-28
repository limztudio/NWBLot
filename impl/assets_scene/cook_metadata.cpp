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

static constexpr AStringView s_StartSceneField = "start_scene";
static constexpr AStringView s_SubsystemsField = "subsystems";
static constexpr AStringView s_EntitiesField = "entities";
static constexpr AStringView s_AssetsField = "assets";
static constexpr AStringView s_ComponentsField = "components";


template<typename TAsset>
[[nodiscard]] static Expected<Core::Assets::AssetVector<Core::Assets::AssetRef<TAsset>>> ParseAssetRefList(
    const Path& nwbFilePath,
    const AStringView diagnosticPrefix,
    const Value& listValue,
    const AStringView fieldLabel,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!listValue.isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a list"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    const auto& items = listValue.asList();
    Core::Assets::AssetVector<Core::Assets::AssetRef<TAsset>> refs(arena);
    refs.reserve(items.size());
    for(const Value& item : items){
        if(!item.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entries must be strings"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
            return MakeUnexpected(Failure{});
        }
        const AStringView text(item.asString().data(), item.asString().size());
        const auto parsedRef = Core::Assets::ParseMetadataReservedAssetRefText<TAsset>(nwbFilePath, diagnosticPrefix, fieldLabel, text, scratchArena);
        if(!parsedRef)
            return MakeUnexpected(Failure{});
        refs.push_back(*parsedRef);
    }
    return refs;
}

[[nodiscard]] static Expected<SceneAsset::AssetMap> ParseAssetsMap(
    const Path& nwbFilePath,
    const Value& mapValue,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!mapValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a map"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_AssetsField));
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
        if(!assetValue.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entries must be strings"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(nicknameView));
            return MakeUnexpected(Failure{});
        }
        const AStringView text(assetValue.asString().data(), assetValue.asString().size());
        const auto parsedRef = Core::Assets::ParseMetadataReservedAssetRefText<Core::Assets::IAsset>(nwbFilePath, s_SceneDiagnosticPrefix, nicknameView, text, scratchArena);
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


Expected<EntityCookEntry> ParseEntityCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_scene_cook_metadata;

    EntityCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_EntityDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_EntityDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_EntityDiagnosticPrefix, { s_ComponentsField }))
        return MakeUnexpected(Failure{});

    const Core::Metascript::Value* componentsValue = asset.findField(s_ComponentsField);
    if(!componentsValue){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required"), ConvertString(s_EntityDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_ComponentsField));
        return MakeUnexpected(Failure{});
    }
    auto components = ParseAssetRefList<IComponent>(nwbFilePath, s_EntityDiagnosticPrefix, *componentsValue, s_ComponentsField, arena, scratchArena);
    if(!components)
        return MakeUnexpected(Failure{});
    entry.components = Move(*components);

    EntityAsset candidate(arena, entry.virtualPath);
    candidate.setComponents(EntityAsset::ComponentRefVector(entry.components));
    if(!candidate.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});

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


Expected<ProjectCookEntry> ParseProjectCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_scene_cook_metadata;

    ProjectCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_ProjectDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_ProjectDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_ProjectDiagnosticPrefix, { s_StartSceneField }))
        return MakeUnexpected(Failure{});

    const auto startSceneField = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_ProjectDiagnosticPrefix, s_StartSceneField, true);
    if(!startSceneField)
        return MakeUnexpected(Failure{});
    const auto startSceneRef = Core::Assets::ParseMetadataReservedAssetRefText<SceneAsset>(nwbFilePath, s_ProjectDiagnosticPrefix, s_StartSceneField, startSceneField->text, scratchArena);
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


Expected<SceneCookEntry> ParseSceneCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_scene_cook_metadata;

    SceneCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_SceneDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_SceneDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_SceneDiagnosticPrefix, { s_SubsystemsField, s_EntitiesField, s_AssetsField }))
        return MakeUnexpected(Failure{});

    const Core::Metascript::Value* subsystemsValue = asset.findField(s_SubsystemsField);
    if(!subsystemsValue){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_SubsystemsField));
        return MakeUnexpected(Failure{});
    }
    auto subsystems = ParseAssetRefList<ISubsystem>(nwbFilePath, s_SceneDiagnosticPrefix, *subsystemsValue, s_SubsystemsField, arena, scratchArena);
    if(!subsystems)
        return MakeUnexpected(Failure{});
    SubsystemAssetRuntime::DeduplicateSubsystemRefs(*subsystems);
    entry.subsystems = Move(*subsystems);

    if(const Core::Metascript::Value* entitiesValue = asset.findField(s_EntitiesField)){
        auto entities = ParseAssetRefList<EntityAsset>(nwbFilePath, s_SceneDiagnosticPrefix, *entitiesValue, s_EntitiesField, arena, scratchArena);
        if(!entities)
            return MakeUnexpected(Failure{});
        entry.entities = Move(*entities);
    }

    if(const Core::Metascript::Value* assetsValue = asset.findField(s_AssetsField)){
        auto assets = ParseAssetsMap(nwbFilePath, *assetsValue, arena, scratchArena);
        if(!assets)
            return MakeUnexpected(Failure{});
        entry.assets = Move(*assets);
    }

    SceneAsset candidate(arena, entry.virtualPath);
    candidate.setContents(
        SceneAsset::SubsystemRefVector(entry.subsystems),
        SceneAsset::AssetMap(entry.assets),
        SceneAsset::EntityRefVector(entry.entities)
    );
    if(!candidate.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});

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
