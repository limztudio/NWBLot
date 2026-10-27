// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/cook_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_scene_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<TransformComponentCookEntry> ParseTransformComponentDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseTransformComponentCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static Expected<TransformComponentCookEntry> ParseTransformComponentValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseTransformComponentCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

static bool ExtractTransformComponentDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector&){
    const auto entry = context.document
        ? ParseTransformComponentCookMetadata(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, context.arena, context.scratchArena)
        : ParseTransformComponentCookMetadata(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
    return entry.has_value();
}

static bool RegisterTransformComponentCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<TransformComponentCookEntry, TransformComponentAsset, TypedComponentAssetCodec<TransformComponentAsset>>(
        registry,
        ComponentBinaryPayload::s_TransformComponentAssetKindLabel,
        &ParseTransformComponentDocument,
        &ParseTransformComponentValue,
        &BuildTransformComponentAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<CameraComponentCookEntry> ParseCameraComponentDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseCameraComponentCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static Expected<CameraComponentCookEntry> ParseCameraComponentValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseCameraComponentCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

static bool ExtractCameraComponentDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector&){
    const auto entry = context.document
        ? ParseCameraComponentCookMetadata(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, context.arena, context.scratchArena)
        : ParseCameraComponentCookMetadata(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
    return entry.has_value();
}

static bool RegisterCameraComponentCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<CameraComponentCookEntry, CameraComponentAsset, TypedComponentAssetCodec<CameraComponentAsset>>(
        registry,
        ComponentBinaryPayload::s_CameraComponentAssetKindLabel,
        &ParseCameraComponentDocument,
        &ParseCameraComponentValue,
        &BuildCameraComponentAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<LightComponentCookEntry> ParseLightComponentDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseLightComponentCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static Expected<LightComponentCookEntry> ParseLightComponentValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseLightComponentCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

static bool ExtractLightComponentDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector&){
    const auto entry = context.document
        ? ParseLightComponentCookMetadata(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, context.arena, context.scratchArena)
        : ParseLightComponentCookMetadata(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
    return entry.has_value();
}

static bool RegisterLightComponentCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<LightComponentCookEntry, LightComponentAsset, TypedComponentAssetCodec<LightComponentAsset>>(
        registry,
        ComponentBinaryPayload::s_LightComponentAssetKindLabel,
        &ParseLightComponentDocument,
        &ParseLightComponentValue,
        &BuildLightComponentAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<EntityCookEntry> ParseEntityDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseEntityCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static Expected<EntityCookEntry> ParseEntityValue(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::CookEntryParseContext& context){
    return ParseEntityCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

static bool ExtractEntityDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto entityEntry = context.document
        ? ParseEntityCookMetadata(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, context.arena, context.scratchArena)
        : ParseEntityCookMetadata(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
    if(!entityEntry)
        return false;

    for(const EntityAsset::ComponentRef& componentRef : entityEntry->components){
        if(!collector.addAssetDependency(componentRef.name(), IComponent::AssetTypeName()))
            return false;
    }
    return true;
}

static bool RegisterEntityCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<EntityCookEntry, EntityAsset, EntityAssetCodec>(
        registry,
        EntityBinaryPayload::s_EntityAssetKindLabel,
        &ParseEntityDocument,
        &ParseEntityValue,
        &BuildEntityAsset
    );
}


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

static Expected<ProjectCookEntry> ParseProjectValue(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::CookEntryParseContext& context){
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

static Expected<SceneCookEntry> ParseSceneValue(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::CookEntryParseContext& context){
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
        if(!collector.addAssetDependency(subsystemRef.name(), ISubsystem::AssetTypeName()))
            return false;
    }

    for(const SceneAsset::EntityRef& entityRef : sceneEntry->entities){
        if(!collector.addAssetDependency(entityRef.name(), EntityAsset::AssetTypeName()))
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
        registry.registerComponentType(TransformComponentAsset::AssetTypeName(), &ExtractTransformComponentDependencies)
        && registry.registerComponentType(CameraComponentAsset::AssetTypeName(), &ExtractCameraComponentDependencies)
        && registry.registerComponentType(LightComponentAsset::AssetTypeName(), &ExtractLightComponentDependencies)
        && registry.registerType(EntityAsset::AssetTypeName(), &ExtractEntityDependencies)
        && registry.registerType(ProjectAsset::AssetTypeName(), &ExtractProjectDependencies)
        && registry.registerType(SceneAsset::AssetTypeName(), &ExtractSceneDependencies)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_TransformComponentCookEntryRegistrar, RegisterTransformComponentCookEntry);
NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_CameraComponentCookEntryRegistrar, RegisterCameraComponentCookEntry);
NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_LightComponentCookEntryRegistrar, RegisterLightComponentCookEntry);
NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_EntityCookEntryRegistrar, RegisterEntityCookEntry);
NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_ProjectCookEntryRegistrar, RegisterProjectCookEntry);
NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_SceneCookEntryRegistrar, RegisterSceneCookEntry);
NWB_DEFINE_COOK_DEPENDENCY_REGISTRAR(s_SceneCookDependencyRegistrar, RegisterSceneCookDependencies);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

