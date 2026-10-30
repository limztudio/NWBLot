// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <impl/assets_scene/component_assets.adhd.h>

#include <core/assets/cook_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_scene_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace SceneAssetCook;

static b8 RegisterTransformComponentCookEntry(Core::Assets::CookEntryRegistry& registry){
    return RegisterComponentCookEntry<
        TransformComponentCookEntry,
        TransformComponentAsset,
        &ParseTransformComponentCookMetadata,
        s_TransformDiagnosticPrefix
    >(registry);
}

static b8 RegisterCameraComponentCookEntry(Core::Assets::CookEntryRegistry& registry){
    return RegisterComponentCookEntry<
        CameraComponentCookEntry,
        CameraComponentAsset,
        &ParseCameraComponentCookMetadata,
        s_CameraDiagnosticPrefix
    >(registry);
}

static b8 RegisterLightComponentCookEntry(Core::Assets::CookEntryRegistry& registry){
    return RegisterComponentCookEntry<
        LightComponentCookEntry,
        LightComponentAsset,
        &ParseLightComponentCookMetadata,
        s_LightDiagnosticPrefix
    >(registry);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 ExtractEntityDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto entityEntry = ParseCookDependencyEntry<EntityCookEntry, &ParseEntityCookMetadata>(context, s_EntityDiagnosticPrefix);
    if(!entityEntry)
        return false;

    for(const EntityAsset::ComponentRef& componentRef : entityEntry->components){
        if(!collector.addAssetDependency(componentRef.name(), IComponent::AssetTypeName()))
            return false;
    }
    return true;
}

static b8 RegisterEntityCookEntry(Core::Assets::CookEntryRegistry& registry){
    Core::Assets::RegisterCompositeRootAssetType(EntityAsset::AssetTypeName());
    return Core::Assets::RegisterDocumentValueCookEntry<EntityCookEntry, EntityAsset, EntityAssetCodec>(
        registry,
        s_EntityAssetKindLabel,
        &ParseCookEntryDocument<EntityCookEntry, &ParseEntityCookMetadata, s_EntityDiagnosticPrefix>,
        &ParseCookEntryValue<EntityCookEntry, &ParseEntityCookMetadata>,
        &BuildEntityAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 ExtractProjectDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto projectEntry = ParseCookDependencyEntry<ProjectCookEntry, &ParseProjectCookMetadata>(context, s_ProjectDiagnosticPrefix);
    if(!projectEntry)
        return false;
    return collector.addAssetDependency(projectEntry->startScene.name(), SceneAsset::AssetTypeName());
}

static b8 RegisterProjectCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<ProjectCookEntry, ProjectAsset, ProjectAssetCodec>(
        registry,
        s_ProjectAssetKindLabel,
        &ParseCookEntryDocument<ProjectCookEntry, &ParseProjectCookMetadata, s_ProjectDiagnosticPrefix>,
        &ParseCookEntryValue<ProjectCookEntry, &ParseProjectCookMetadata>,
        &BuildProjectAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 ExtractSceneDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto sceneEntry = ParseCookDependencyEntry<SceneCookEntry, &ParseSceneCookMetadata>(context, s_SceneDiagnosticPrefix);
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

static b8 RegisterSceneCookEntry(Core::Assets::CookEntryRegistry& registry){
    Core::Assets::RegisterCompositeRootAssetType(SceneAsset::AssetTypeName());
    return Core::Assets::RegisterDocumentValueCookEntry<SceneCookEntry, SceneAsset, SceneAssetCodec>(
        registry,
        s_SceneAssetKindLabel,
        &ParseCookEntryDocument<SceneCookEntry, &ParseSceneCookMetadata, s_SceneDiagnosticPrefix>,
        &ParseCookEntryValue<SceneCookEntry, &ParseSceneCookMetadata>,
        &BuildSceneAsset
    );
}

static b8 RegisterSceneCookDependencies(Core::Assets::CookDependencyRegistry& registry){
    Core::Assets::RegisterCompositeRootAssetType(EntityAsset::AssetTypeName());
    Core::Assets::RegisterCompositeRootAssetType(SceneAsset::AssetTypeName());
    return
        registry.registerDefaultEntryAsset(s_DefaultProjectVirtualPath, ProjectAsset::AssetTypeName(), s_DefaultProjectVirtualPathText)
        && registry.registerType(TransformComponentAsset::AssetTypeName(), &ExtractLeafComponentDependencies<TransformComponentCookEntry, &ParseTransformComponentCookMetadata, s_TransformDiagnosticPrefix>, IComponent::AssetTypeName())
        && registry.registerType(CameraComponentAsset::AssetTypeName(), &ExtractLeafComponentDependencies<CameraComponentCookEntry, &ParseCameraComponentCookMetadata, s_CameraDiagnosticPrefix>, IComponent::AssetTypeName())
        && registry.registerType(LightComponentAsset::AssetTypeName(), &ExtractLeafComponentDependencies<LightComponentCookEntry, &ParseLightComponentCookMetadata, s_LightDiagnosticPrefix>, IComponent::AssetTypeName())
        && registry.registerType(EntityAsset::AssetTypeName(), &ExtractEntityDependencies)
        && registry.registerType(ProjectAsset::AssetTypeName(), &ExtractProjectDependencies)
        && registry.registerType(SceneAsset::AssetTypeName(), &ExtractSceneDependencies)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::Assets::CompositeRootAssetTypeAutoRegistrar s_EntityCompositeRootRegistrar(EntityAsset::AssetTypeName());
static Core::Assets::CompositeRootAssetTypeAutoRegistrar s_SceneCompositeRootRegistrar(SceneAsset::AssetTypeName());
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

