// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_scene_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace SceneAssetCook;


static bool RegisterTransformComponentCookEntry(Core::Assets::CookEntryRegistry& registry){
    return RegisterComponentCookEntry<
        TransformComponentCookEntry,
        TransformComponentAsset,
        &ParseTransformComponentCookMetadata,
        s_TransformDiagnosticPrefix
    >(registry, ComponentBinaryPayload::s_TransformComponentAssetKindLabel);
}

static bool RegisterCameraComponentCookEntry(Core::Assets::CookEntryRegistry& registry){
    return RegisterComponentCookEntry<
        CameraComponentCookEntry,
        CameraComponentAsset,
        &ParseCameraComponentCookMetadata,
        s_CameraDiagnosticPrefix
    >(registry, ComponentBinaryPayload::s_CameraComponentAssetKindLabel);
}

static bool RegisterLightComponentCookEntry(Core::Assets::CookEntryRegistry& registry){
    return RegisterComponentCookEntry<
        LightComponentCookEntry,
        LightComponentAsset,
        &ParseLightComponentCookMetadata,
        s_LightDiagnosticPrefix
    >(registry, ComponentBinaryPayload::s_LightComponentAssetKindLabel);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ExtractEntityDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto entityEntry = ParseCookDependencyEntry<EntityCookEntry, &ParseEntityCookMetadata>(context, s_EntityDiagnosticPrefix);
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
        &ParseCookEntryDocument<EntityCookEntry, &ParseEntityCookMetadata, s_EntityDiagnosticPrefix>,
        &ParseCookEntryValue<EntityCookEntry, &ParseEntityCookMetadata>,
        &BuildEntityAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ExtractProjectDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto projectEntry = ParseCookDependencyEntry<ProjectCookEntry, &ParseProjectCookMetadata>(context, s_ProjectDiagnosticPrefix);
    if(!projectEntry)
        return false;
    return collector.addAssetDependency(projectEntry->startScene.name(), SceneAsset::AssetTypeName());
}

static bool RegisterProjectCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<ProjectCookEntry, ProjectAsset, ProjectAssetCodec>(
        registry,
        ProjectBinaryPayload::s_ProjectAssetKindLabel,
        &ParseCookEntryDocument<ProjectCookEntry, &ParseProjectCookMetadata, s_ProjectDiagnosticPrefix>,
        &ParseCookEntryValue<ProjectCookEntry, &ParseProjectCookMetadata>,
        &BuildProjectAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ExtractSceneDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
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

static bool RegisterSceneCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<SceneCookEntry, SceneAsset, SceneAssetCodec>(
        registry,
        SceneBinaryPayload::s_SceneAssetKindLabel,
        &ParseCookEntryDocument<SceneCookEntry, &ParseSceneCookMetadata, s_SceneDiagnosticPrefix>,
        &ParseCookEntryValue<SceneCookEntry, &ParseSceneCookMetadata>,
        &BuildSceneAsset
    );
}

static bool RegisterSceneCookDependencies(Core::Assets::CookDependencyRegistry& registry){
    return
        registry.registerComponentType(
            TransformComponentAsset::AssetTypeName(),
            &ExtractLeafComponentDependencies<TransformComponentCookEntry, &ParseTransformComponentCookMetadata, s_TransformDiagnosticPrefix>
        )
        && registry.registerComponentType(
            CameraComponentAsset::AssetTypeName(),
            &ExtractLeafComponentDependencies<CameraComponentCookEntry, &ParseCameraComponentCookMetadata, s_CameraDiagnosticPrefix>
        )
        && registry.registerComponentType(
            LightComponentAsset::AssetTypeName(),
            &ExtractLeafComponentDependencies<LightComponentCookEntry, &ParseLightComponentCookMetadata, s_LightDiagnosticPrefix>
        )
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
