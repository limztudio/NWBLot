// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_graphics/volume_entry.adhd.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Impl{
namespace __hidden_assets_graphics_volume_entry_registration{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::Assets::CookEntryAutoRegistrar s_GraphicsComponentEntriesRegistrar([](Core::Assets::CookEntryRegistry& registry){
    const auto label = FOUNDATION_TEXT("component");
    return Core::Assets::RegisterCookEntry<ModelComponentCookEntry, ModelComponentAsset, ModelComponentCodec, s_ModelComponentDiagnosticPrefix, true>(
            registry, label, true, NWB_COOK_DISPATCH(ParseModelComponentCookMetadata), NWB_COOK_DISPATCH(CookModelComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<MeshComponentCookEntry, MeshComponentAsset, MeshComponentCodec, s_MeshComponentDiagnosticPrefix, true>(
            registry, label, true, NWB_COOK_DISPATCH(ParseMeshComponentCookMetadata), NWB_COOK_DISPATCH(CookMeshComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<RendererComponentCookEntry, RendererComponentAsset, RendererComponentCodec, s_RendererComponentDiagnosticPrefix, true>(
            registry, label, true, NWB_COOK_DISPATCH(ParseRendererComponentCookMetadata), NWB_COOK_DISPATCH(CookRendererComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<MaterialInstanceComponentCookEntry, MaterialInstanceComponentAsset, MaterialInstanceComponentCodec, s_MaterialInstanceComponentDiagnosticPrefix, true>(
            registry, label, true, NWB_COOK_DISPATCH(ParseMaterialInstanceComponentCookMetadata), NWB_COOK_DISPATCH(CookMaterialInstanceComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<SkeletonJointPaletteComponentCookEntry, SkeletonJointPaletteComponentAsset, SkeletonJointPaletteComponentCodec, s_SkeletonJointPaletteComponentDiagnosticPrefix, true>(
            registry, label, true, NWB_COOK_DISPATCH(ParseSkeletonJointPaletteComponentCookMetadata), NWB_COOK_DISPATCH(CookSkeletonJointPaletteComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<SkeletonPoseComponentCookEntry, SkeletonPoseComponentAsset, SkeletonPoseComponentCodec, s_SkeletonPoseComponentDiagnosticPrefix, true>(
            registry, label, true, NWB_COOK_DISPATCH(ParseSkeletonPoseComponentCookMetadata), NWB_COOK_DISPATCH(CookSkeletonPoseComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<SkinnedMeshBindingComponentCookEntry, SkinnedMeshBindingComponentAsset, SkinnedMeshBindingComponentCodec, s_SkinnedMeshBindingComponentDiagnosticPrefix, true>(
            registry, label, true, NWB_COOK_DISPATCH(ParseSkinnedMeshBindingComponentCookMetadata), NWB_COOK_DISPATCH(CookSkinnedMeshBindingComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<CsgReceiverComponentCookEntry, CsgReceiverComponentAsset, CsgReceiverComponentCodec, s_CsgReceiverComponentDiagnosticPrefix, true>(
            registry, label, true, NWB_COOK_DISPATCH(ParseCsgReceiverComponentCookMetadata), NWB_COOK_DISPATCH(CookCsgReceiverComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<StaticCsgMeshComponentCookEntry, StaticCsgMeshComponentAsset, StaticCsgMeshComponentCodec, s_StaticCsgMeshComponentDiagnosticPrefix, true>(
            registry, label, true, NWB_COOK_DISPATCH(ParseStaticCsgMeshComponentCookMetadata), NWB_COOK_DISPATCH(CookStaticCsgMeshComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<SkinnedCsgMeshComponentCookEntry, SkinnedCsgMeshComponentAsset, SkinnedCsgMeshComponentCodec, s_SkinnedCsgMeshComponentDiagnosticPrefix, true>(
            registry, label, true, NWB_COOK_DISPATCH(ParseSkinnedCsgMeshComponentCookMetadata), NWB_COOK_DISPATCH(CookSkinnedCsgMeshComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<CsgCutterComponentCookEntry, CsgCutterComponentAsset, CsgCutterComponentCodec, s_CsgCutterComponentDiagnosticPrefix, true>(
            registry, label, true, NWB_COOK_DISPATCH(ParseCsgCutterComponentCookMetadata), NWB_COOK_DISPATCH(CookCsgCutterComponentVolumeEntry)
        );
});

static Core::Assets::CookDependencyAutoRegistrar s_GraphicsComponentDependencyRegistrar(&RegisterGraphicsComponentCookDependencies);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

