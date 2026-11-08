// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinning_benchmark_scene.h"

#include <impl/ecs_scene/module.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SkinningBenchmarkScene::Populate(
    const u32 characterCount,
    const b8 staticPreview,
    const SmokeModelRef& model,
    const SmokeMaterialRef& material,
    const Name skeletonObject,
    const Float4& cameraPosition,
    Core::ECS::World& world,
    Vector<Core::ECS::EntityID, Core::Alloc::GlobalArena>& skeletonEntities,
    Core::ECS::EntityID& cameraEntity
){
    auto activeCameraEntity = world.createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<Impl::Scene::ActiveCameraComponent>();
    activeCamera.camera = Impl::Scene::CreateSceneCameraEntity(world, cameraPosition);
    cameraEntity = activeCamera.camera;
    const Core::ECS::EntityID directionalLight = Impl::Scene::CreateDirectionalLightEntity(world, -0.65f, 0.65f, 0.0f, Float4(1.0f, 0.96f, 0.88f), 2.0f);
    if(!activeCamera.camera.valid() || !directionalLight.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: failed to create the benchmark camera or directional light"));
        return false;
    }

    skeletonEntities.reserve(characterCount);
    Core::Alloc::ScratchArena scratchArena{Name(NWB::Tests::Smoke::s_SmokeNameTestsSkinningCullingModelOwners)};
    Vector<Core::ECS::EntityID, Core::Alloc::ScratchArena> modelOwners(scratchArena);
    modelOwners.reserve(characterCount);
    for(u32 characterIndex = 0u; characterIndex < characterCount; ++characterIndex){
        auto entity = world.createEntity();
        auto& transform = entity.addComponent<Impl::Scene::TransformComponent>();
        const SIMDVectorSingle position = staticPreview ? VectorZero() : VectorScale(VectorSubtract(VectorSet(static_cast<f32>(characterIndex), 0.0f, 0.0f, 0.0f), VectorSet(1.0f, 0.0f, 0.0f, 0.0f)), 1.15f);
        StoreFloat(position, transform.position);
        transform.scale = Float4(1.0f, 1.0f, 1.0f, 0.0f);
        auto& modelComponent = entity.addComponent<Impl::ModelComponent>();
        modelComponent.model = model;
        auto& renderer = entity.addComponent<Impl::RendererComponent>();
        renderer.material = material;
        modelOwners.push_back(entity.id());
    }

    SyncSmokeModelRuntimes(world);
    for(const Core::ECS::EntityID owner : modelOwners){
        const Core::ECS::EntityID skeletonEntity = FindSpawnedModelObject(owner, skeletonObject, Impl::ModelObjectKind::Skeleton, world);
        if(!skeletonEntity.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: failed to find spawned benchmark skeleton object"));
            return false;
        }
        skeletonEntities.push_back(skeletonEntity);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

