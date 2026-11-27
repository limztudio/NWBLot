// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_physics/volume_entry.adhd.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Impl{
namespace __hidden_assets_physics_volume_entry_registration{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::Assets::CookEntryAutoRegistrar s_PhysicsCookEntriesRegistrar([](Core::Assets::CookEntryRegistry& registry){
    const auto componentLabel = FOUNDATION_TEXT("component");
    return Core::Assets::RegisterCookEntry<RigidBodyComponentCookEntry, RigidBodyComponentAsset, RigidBodyComponentCodec, s_RigidBodyComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseRigidBodyComponentCookMetadata), NWB_COOK_DISPATCH(CookRigidBodyComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<ColliderComponentCookEntry, ColliderComponentAsset, ColliderComponentCodec, s_ColliderComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseColliderComponentCookMetadata), NWB_COOK_DISPATCH(CookColliderComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<JointComponentCookEntry, JointComponentAsset, JointComponentCodec, s_JointComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseJointComponentCookMetadata), NWB_COOK_DISPATCH(CookJointComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<PhysicsWorldConfigComponentCookEntry, PhysicsWorldConfigComponentAsset, PhysicsWorldConfigComponentCodec, s_PhysicsWorldConfigComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParsePhysicsWorldConfigComponentCookMetadata), NWB_COOK_DISPATCH(CookPhysicsWorldConfigComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<PhysicsParentComponentCookEntry, PhysicsParentComponentAsset, PhysicsParentComponentCodec, s_PhysicsParentComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParsePhysicsParentComponentCookMetadata), NWB_COOK_DISPATCH(CookPhysicsParentComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<CharacterControllerComponentCookEntry, CharacterControllerComponentAsset, CharacterControllerComponentCodec, s_CharacterControllerComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseCharacterControllerComponentCookMetadata), NWB_COOK_DISPATCH(CookCharacterControllerComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<VehicleComponentCookEntry, VehicleComponentAsset, VehicleComponentCodec, s_VehicleComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseVehicleComponentCookMetadata), NWB_COOK_DISPATCH(CookVehicleComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<RagdollComponentCookEntry, RagdollComponentAsset, RagdollComponentCodec, s_RagdollComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseRagdollComponentCookMetadata), NWB_COOK_DISPATCH(CookRagdollComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<FluidVolumeComponentCookEntry, FluidVolumeComponentAsset, FluidVolumeComponentCodec, s_FluidVolumeComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseFluidVolumeComponentCookMetadata), NWB_COOK_DISPATCH(CookFluidVolumeComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<ForceFieldComponentCookEntry, ForceFieldComponentAsset, ForceFieldComponentCodec, s_ForceFieldComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseForceFieldComponentCookMetadata), NWB_COOK_DISPATCH(CookForceFieldComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<ArticulationComponentCookEntry, ArticulationComponentAsset, ArticulationComponentCodec, s_ArticulationComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseArticulationComponentCookMetadata), NWB_COOK_DISPATCH(CookArticulationComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<SoftBodyComponentCookEntry, SoftBodyComponentAsset, SoftBodyComponentCodec, s_SoftBodyComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseSoftBodyComponentCookMetadata), NWB_COOK_DISPATCH(CookSoftBodyComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<GpuRenderTransformExportComponentCookEntry, GpuRenderTransformExportComponentAsset, GpuRenderTransformExportComponentCodec, s_GpuRenderTransformExportComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseGpuRenderTransformExportComponentCookMetadata), NWB_COOK_DISPATCH(CookGpuRenderTransformExportComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<PhysicsMaterialCookEntry, PhysicsMaterialAsset, PhysicsMaterialAssetCodec, s_PhysicsMaterialDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("physics_material"), true, NWB_COOK_DISPATCH(ParsePhysicsMaterialCookMetadata), NWB_COOK_DISPATCH(CookPhysicsMaterialVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<CollisionLayerMatrixCookEntry, CollisionLayerMatrixAsset, CollisionLayerMatrixAssetCodec, s_CollisionLayerMatrixDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("collision_layer_matrix"), true, NWB_COOK_DISPATCH(ParseCollisionLayerMatrixCookMetadata), NWB_COOK_DISPATCH(CookCollisionLayerMatrixVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<ConvexHullCookEntry, ConvexHullAsset, ConvexHullAssetCodec, s_ConvexHullDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("convex_hull"), true, NWB_COOK_DISPATCH(ParseConvexHullCookMetadata), NWB_COOK_DISPATCH(CookConvexHullVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<CompoundHullCookEntry, CompoundHullAsset, CompoundHullAssetCodec, s_CompoundHullDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("compound_hull"), true, NWB_COOK_DISPATCH(ParseCompoundHullCookMetadata), NWB_COOK_DISPATCH(CookCompoundHullVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<CollisionMeshCookEntry, CollisionMeshAsset, CollisionMeshAssetCodec, s_CollisionMeshDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("collision_mesh"), true, NWB_COOK_DISPATCH(ParseCollisionMeshCookMetadata), NWB_COOK_DISPATCH(CookCollisionMeshVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<HeightfieldCookEntry, HeightfieldAsset, HeightfieldAssetCodec, s_HeightfieldDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("heightfield"), true, NWB_COOK_DISPATCH(ParseHeightfieldCookMetadata), NWB_COOK_DISPATCH(CookHeightfieldVolumeEntry)
        );
});

static Core::Assets::CookDependencyAutoRegistrar s_PhysicsDependencyRegistrar(&RegisterPhysicsComponentCookDependencies);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

