// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_scene/volume_entry.adhd.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Impl{
namespace __hidden_assets_scene_volume_entry_registration{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::Assets::CookEntryAutoRegistrar s_SceneCookEntriesRegistrar([](Core::Assets::CookEntryRegistry& registry){
    const auto componentLabel = FOUNDATION_TEXT("component");
    return Core::Assets::RegisterCookEntry<TransformComponentCookEntry, TransformComponentAsset, TransformComponentCodec, s_TransformDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseTransformComponentCookMetadata), NWB_COOK_DISPATCH(CookTransformVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<LocalTransformComponentCookEntry, LocalTransformComponentAsset, LocalTransformComponentCodec, s_LocalTransformDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseLocalTransformComponentCookMetadata), NWB_COOK_DISPATCH(CookLocalTransformVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<SceneParentComponentCookEntry, SceneParentComponentAsset, SceneParentComponentCodec, s_SceneParentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseSceneParentComponentCookMetadata), NWB_COOK_DISPATCH(CookSceneParentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<CameraComponentCookEntry, CameraComponentAsset, CameraComponentCodec, s_CameraDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseCameraComponentCookMetadata), NWB_COOK_DISPATCH(CookCameraVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<LightComponentCookEntry, LightComponentAsset, LightComponentCodec, s_LightDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseLightComponentCookMetadata), NWB_COOK_DISPATCH(CookLightVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<ActiveCameraComponentCookEntry, ActiveCameraComponentAsset, ActiveCameraComponentCodec, s_ActiveCameraDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseActiveCameraComponentCookMetadata), NWB_COOK_DISPATCH(CookActiveCameraComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<EntityCookEntry, EntityAsset, EntityAssetCodec, s_EntityDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("entity"), true, NWB_COOK_DISPATCH(ParseEntityCookMetadata), NWB_COOK_DISPATCH(CookEntityVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<ProjectCookEntry, ProjectAsset, ProjectAssetCodec, s_ProjectDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("project"), true, NWB_COOK_DISPATCH(ParseProjectCookMetadata), NWB_COOK_DISPATCH(CookProjectVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<SceneCookEntry, SceneAsset, SceneAssetCodec, s_SceneDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("scene"), true, NWB_COOK_DISPATCH(ParseSceneCookMetadata), NWB_COOK_DISPATCH(CookSceneVolumeEntry)
        );
});

static Core::Assets::CookDependencyAutoRegistrar s_SceneDependencyRegistrar(&RegisterSceneCookDependencies);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

