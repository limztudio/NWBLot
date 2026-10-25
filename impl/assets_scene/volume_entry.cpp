// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"

#include <impl/assets_subsystem/cook.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/volume/volume_prepare_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_scene_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<ProjectCookEntry> ParseProjectDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseProjectCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static Expected<ProjectCookEntry> ParseProjectValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseProjectCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

static bool ExtractProjectDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto projectEntry = context.document
        ? ParseProjectCookMetadata(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, context.arena, context.scratchArena)
        : ParseProjectCookMetadata(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
    if(!projectEntry)
        return false;
    return collector.addAssetDependency(projectEntry->startScene.name(), SceneAsset::AssetTypeName());
}

static bool RegisterProjectCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<ProjectCookEntry, ProjectAsset, ProjectAssetCodec>(
        registry,
        ProjectBinaryPayload::s_ProjectAssetKindLabel,
        &ParseProjectDocument,
        &ParseProjectValue,
        &BuildProjectAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<SceneCookEntry> ParseSceneDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSceneCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static Expected<SceneCookEntry> ParseSceneValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSceneCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

static bool ExtractSceneDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto sceneEntry = context.document
        ? ParseSceneCookMetadata(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, context.arena, context.scratchArena)
        : ParseSceneCookMetadata(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
    if(!sceneEntry)
        return false;

    for(const SceneAsset::SubsystemRef& subsystemRef : sceneEntry->subsystems){
        if(!collector.addSubsystemDependency(subsystemRef.name()))
            return false;
    }

    Vector<Pair<Name, Name>, Core::Alloc::ScratchArena> sortedAssets(context.scratchArena);
    sortedAssets.reserve(sceneEntry->assets.size());
    for(const auto& [nickname, assetRef] : sceneEntry->assets)
        sortedAssets.emplace_back(nickname, assetRef.name());
    Sort(sortedAssets.begin(), sortedAssets.end(), [](const Pair<Name, Name>& lhs, const Pair<Name, Name>& rhs)noexcept{
        if(lhs.first() != rhs.first())
            return lhs.first() < rhs.first();
        return lhs.second() < rhs.second();
    });
    for(const Pair<Name, Name>& entry : sortedAssets){
        if(!collector.addAssetDependency(entry.second()))
            return false;
    }
    return true;
}

static void AppendSubsystemIfAbsent(SceneAsset::SubsystemRefVector& subsystems, const SceneAsset::SubsystemRef& subsystem){
    if(!subsystem.valid())
        return;
    for(const SceneAsset::SubsystemRef& existing : subsystems){
        if(existing == subsystem)
            return;
    }
    subsystems.push_back(subsystem);
}

static bool PrepareSceneVolumeEntries(Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareContext& context){
    auto& entryRegistry = context.parsedMetadata.entryRegistry;
    const Core::Assets::ICookEntryBucket* sceneBucket = entryRegistry.find(SceneAsset::AssetTypeName());
    if(!sceneBucket || sceneBucket->size() == 0u)
        return true;

    const Core::Assets::ICookEntryBucket* subsystemBucket = entryRegistry.find(Subsystem::AssetTypeName());
    if(!subsystemBucket || subsystemBucket->size() == 0u)
        return true;

    const auto& subsystemEntries = entryRegistry.entries<SubsystemCookEntry>(Subsystem::AssetTypeName());
    for(SceneCookEntry& scene : entryRegistry.entries<SceneCookEntry>(SceneAsset::AssetTypeName())){
        for(const SubsystemCookEntry& subsystemEntry : subsystemEntries){
            SceneAsset::SubsystemRef ref;
            ref.virtualPath = subsystemEntry.virtualPath;
            AppendSubsystemIfAbsent(scene.subsystems, ref);
        }
    }

    return true;
}

static bool RegisterSceneCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<SceneCookEntry, SceneAsset, SceneAssetCodec>(
        registry,
        SceneBinaryPayload::s_SceneAssetKindLabel,
        &ParseSceneDocument,
        &ParseSceneValue,
        &BuildSceneAsset
    );
}

static bool RegisterSceneCookDependencies(Core::Assets::CookDependencyRegistry& registry){
    return
        registry.registerType(ProjectAsset::AssetTypeName(), &ExtractProjectDependencies)
        && registry.registerType(SceneAsset::AssetTypeName(), &ExtractSceneDependencies)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_ProjectCookEntryRegistrar, RegisterProjectCookEntry);
NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_SceneCookEntryRegistrar, RegisterSceneCookEntry);
NWB_DEFINE_COOK_DEPENDENCY_REGISTRAR(s_SceneCookDependencyRegistrar, RegisterSceneCookDependencies);
static Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareAutoRegistrar s_SceneVolumePrepareRegistrar(&PrepareSceneVolumeEntries);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

