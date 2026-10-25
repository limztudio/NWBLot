// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "arena_names.h"
#include "binary_payload.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/paths.h>
#include <core/common/log.h>

#include <global/binary.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ProjectAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, ProjectBinaryPayload::s_ProjectSerializeContext))
        return false;

    const ProjectAsset& project = *MakeNotNull(checked_cast<const ProjectAsset*>(&asset));
    if(!project.validatePayload())
        return false;

    ProjectBinaryPayload::ProjectHeaderBinary header;
    header.startSceneHash = project.startScene().name().hash();

    outBinary.clear();
    outBinary.reserve(sizeof(header));
    AppendPOD(outBinary, header);
    return true;
}

bool SceneAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, SceneBinaryPayload::s_SceneSerializeContext))
        return false;

    const SceneAsset& scene = *MakeNotNull(checked_cast<const SceneAsset*>(&asset));
    Core::Alloc::ScratchArena scratchArena(AssetsSceneArenaScope::s_SceneSerializeArena);
    if(!scene.validatePayload(scratchArena))
        return false;

    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> enabledFeatureHashes(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> assetHashes(scratchArena);

    subsystemHashes.reserve(scene.subsystems().size());
    for(const SceneAsset::SubsystemRef& item : scene.subsystems())
        subsystemHashes.push_back(item.name().hash());

    enabledFeatureHashes.reserve(scene.enabledFeatures().size());
    for(const Name& item : scene.enabledFeatures())
        enabledFeatureHashes.push_back(item.hash());

    assetHashes.reserve(scene.assets().size());
    for(const SceneAsset::AssetRef& item : scene.assets())
        assetHashes.push_back(item.name().hash());

    usize reserveBytes = sizeof(SceneBinaryPayload::SceneHeaderBinary);
    const bool canReserve =
        AddBinaryVectorReserveBytes(reserveBytes, subsystemHashes)
        && AddBinaryVectorReserveBytes(reserveBytes, enabledFeatureHashes)
        && AddBinaryVectorReserveBytes(reserveBytes, assetHashes)
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    SceneBinaryPayload::SceneHeaderBinary header;
    header.subsystemCount = static_cast<u64>(subsystemHashes.size());
    header.enabledFeatureCount = static_cast<u64>(enabledFeatureHashes.size());
    header.assetCount = static_cast<u64>(assetHashes.size());
    AppendPOD(outBinary, header);

    return
        Core::Assets::AppendVectorPayload(outBinary, subsystemHashes, SceneBinaryPayload::s_SceneSerializeContext, SceneBinaryPayload::s_SubsystemsPayloadLabel)
        && Core::Assets::AppendVectorPayload(outBinary, enabledFeatureHashes, SceneBinaryPayload::s_SceneSerializeContext, SceneBinaryPayload::s_EnabledFeaturesPayloadLabel)
        && Core::Assets::AppendVectorPayload(outBinary, assetHashes, SceneBinaryPayload::s_SceneSerializeContext, SceneBinaryPayload::s_AssetsPayloadLabel)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

static constexpr AStringView s_StartSceneField = "start_scene";
static constexpr AStringView s_SubsystemsField = "subsystems";
static constexpr AStringView s_EnabledFeaturesField = "enabled_features";
static constexpr AStringView s_AssetsField = "assets";
static constexpr AStringView s_ProjectDiagnosticPrefix = "Project meta";
static constexpr AStringView s_SceneDiagnosticPrefix = "Scene meta";


template<typename TAsset>
[[nodiscard]] static Expected<Core::Assets::AssetRef<TAsset>> ParseAssetRefText(
    const Path& nwbFilePath,
    const AStringView diagnosticPrefix,
    const AStringView fieldLabel,
    const AStringView text,
    Core::Alloc::ScratchArena& scratchArena
){
    if(TrimView(text).empty() || text != TrimView(text)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' must be a non-empty trimmed virtual path"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    if(text.find('\\') != AStringView::npos || !Core::Assets::HasReservedAssetVirtualRoot(text, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' must be an engine/- or project/-rooted virtual path"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    const auto ref = Core::Assets::AssetRef<TAsset>::FromText(text);
    if(!ref || !ref->valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' is invalid"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    return *ref;
}

template<typename TAsset>
[[nodiscard]] static Expected<Core::Assets::AssetVector<Core::Assets::AssetRef<TAsset>>> ParseAssetRefList(
    const Path& nwbFilePath,
    const Value& listValue,
    const AStringView fieldLabel,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!listValue.isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a list"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    const auto& items = listValue.asList();
    Core::Assets::AssetVector<Core::Assets::AssetRef<TAsset>> refs(arena);
    refs.reserve(items.size());
    for(const Value& item : items){
        if(!item.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entries must be strings"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
            return MakeUnexpected(Failure{});
        }
        const AStringView text(item.asString().data(), item.asString().size());
        const auto parsedRef = ParseAssetRefText<TAsset>(nwbFilePath, s_SceneDiagnosticPrefix, fieldLabel, text, scratchArena);
        if(!parsedRef)
            return MakeUnexpected(Failure{});
        refs.push_back(*parsedRef);
    }
    return refs;
}

[[nodiscard]] static Expected<SceneAsset::NameVector> ParseFeatureNameList(
    const Path& nwbFilePath,
    const Value& listValue,
    Core::Assets::AssetArena& arena
){
    if(!listValue.isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a list"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_EnabledFeaturesField));
        return MakeUnexpected(Failure{});
    }
    const auto& items = listValue.asList();
    SceneAsset::NameVector names(arena);
    names.reserve(items.size());
    for(const Value& item : items){
        if(!item.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entries must be strings"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_EnabledFeaturesField));
            return MakeUnexpected(Failure{});
        }
        const AStringView text(item.asString().data(), item.asString().size());
        if(TrimView(text).empty() || text != TrimView(text)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': feature name '{}' must not be empty or padded"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(text));
            return MakeUnexpected(Failure{});
        }
        const auto parsedName = Name::FromText(text);
        if(!parsedName || !*parsedName){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': feature name '{}' contains an embedded null"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(text));
            return MakeUnexpected(Failure{});
        }
        names.push_back(*parsedName);
    }
    return names;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ProjectCookEntry> ParseProjectCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_scene_cook;

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
    const auto startSceneRef = ParseAssetRefText<SceneAsset>(nwbFilePath, s_ProjectDiagnosticPrefix, s_StartSceneField, startSceneField->text, scratchArena);
    if(!startSceneRef)
        return MakeUnexpected(Failure{});
    entry.startScene = *startSceneRef;

    ProjectAsset candidate(arena, entry.virtualPath);
    candidate.setStartScene(entry.startScene);
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});

    return entry;
}

Expected<ProjectCookEntry> ParseProjectCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, __hidden_scene_cook::s_ProjectDiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseProjectCookMetadata(*virtualPathResult, nwbFilePath, **asset, arena, scratchArena);
}

Expected<ProjectAsset> BuildProjectAsset(
    ProjectCookEntry& entry,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena&
){
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
    using namespace __hidden_scene_cook;

    SceneCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_SceneDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_SceneDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_SceneDiagnosticPrefix, { s_SubsystemsField, s_EnabledFeaturesField, s_AssetsField }))
        return MakeUnexpected(Failure{});

    const Core::Metascript::Value* subsystemsValue = asset.findField(s_SubsystemsField);
    if(!subsystemsValue){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_SubsystemsField));
        return MakeUnexpected(Failure{});
    }
    auto subsystems = ParseAssetRefList<Subsystem>(nwbFilePath, *subsystemsValue, s_SubsystemsField, arena, scratchArena);
    if(!subsystems)
        return MakeUnexpected(Failure{});
    entry.subsystems = Move(*subsystems);

    if(const Core::Metascript::Value* enabledFeaturesValue = asset.findField(s_EnabledFeaturesField)){
        auto enabledFeatures = ParseFeatureNameList(nwbFilePath, *enabledFeaturesValue, arena);
        if(!enabledFeatures)
            return MakeUnexpected(Failure{});
        entry.enabledFeatures = Move(*enabledFeatures);
    }

    if(const Core::Metascript::Value* assetsValue = asset.findField(s_AssetsField)){
        auto assets = ParseAssetRefList<Core::Assets::IAsset>(nwbFilePath, *assetsValue, s_AssetsField, arena, scratchArena);
        if(!assets)
            return MakeUnexpected(Failure{});
        entry.assets = Move(*assets);
    }

    SceneAsset candidate(arena, entry.virtualPath);
    candidate.setContents(
        SceneAsset::SubsystemRefVector(entry.subsystems),
        SceneAsset::NameVector(entry.enabledFeatures),
        SceneAsset::AssetRefVector(entry.assets)
    );
    if(!candidate.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});

    return entry;
}

Expected<SceneCookEntry> ParseSceneCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, __hidden_scene_cook::s_SceneDiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseSceneCookMetadata(*virtualPathResult, nwbFilePath, **asset, arena, scratchArena);
}

Expected<SceneAsset> BuildSceneAsset(
    SceneCookEntry& entry,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    SceneAsset asset(arena, entry.virtualPath);
    asset.setContents(Move(entry.subsystems), Move(entry.enabledFeatures), Move(entry.assets));
    if(!asset.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

