// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_scene_helpers.h"

#include <core/ecs/entity.h>
#include <impl/assets_model/asset.h>
#include <impl/ecs_mesh/skinning/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_model_renderer/model_renderer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB{
namespace Tests{
namespace Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SmokeModelRef = Core::Assets::AssetRef<Impl::Model>;


inline void AddSmokeSkinnedRenderSystems(
    Core::ECS::World& world,
    ProjectRuntimeContext& context
){
    const SmokeRenderSystems systems = CreateSmokeRenderSystems(world, context);
    world.addSystem<Impl::ModelSystem>(
        world,
        context.assetManager,
        Impl::CreateModelObjectRendererHooks()
    );
    auto& meshSkinningSystem = world.addSystem<Impl::MeshSkinningSystem>(
        world,
        context.graphics,
        context.assetManager,
        systems.mesh,
        context.shaderPathResolver
    );

    context.graphics.addRenderPassToBack(meshSkinningSystem);
    context.graphics.addRenderPassToBack(systems.renderer);
    context.frameGraphRegistry.registerContributor(systems.renderer);
}

inline void DestroySmokeSkinnedRenderWorld(
    ProjectRuntimeContext& context,
    NotNullUniquePtr<Core::ECS::World>& world
){
    if(!world.owner())
        return;

    auto* meshSkinningSystemPtr = world->getSystem<Impl::MeshSkinningSystem>();
    NWB_ASSERT(meshSkinningSystemPtr);
    Impl::MeshSkinningSystem& meshSkinningSystem = *meshSkinningSystemPtr;
    context.graphics.removeRenderPass(meshSkinningSystem);

    RemoveSmokeRendererSystem(context, *world);
    FinishDestroyingSmokeWorld(context, world);
}

inline void SyncSmokeModelRuntimes(Core::ECS::World& world){
    auto* modelSystemPtr = world.getSystem<Impl::ModelSystem>();
    NWB_ASSERT(modelSystemPtr);
    Impl::ModelSystem& modelSystem = *modelSystemPtr;
    modelSystem.syncModelRuntimes();
}

[[nodiscard]] inline Core::ECS::EntityID CreateTintedModelEntity(
    Core::ECS::World& world,
    Core::Alloc::GlobalArena& arena,
    const SmokeModelRef& model,
    const SmokeMaterialRef& material,
    const AStringView materialInterfacePath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    bool& outTintApplied
){
    outTintApplied = false;

    const SmokeTintedEntitySetup setup = CreateSmokeTintedEntity(
        world,
        arena,
        material,
        materialInterfacePath,
        position,
        scale
    );

    auto& modelComponent = world.entity(setup.entity).addComponent<Impl::ModelComponent>();
    modelComponent.model = model;

    const bool tintApplied = ApplySmokeMaterialTint(world, setup, colorTint);
    outTintApplied = tintApplied;

    return setup.entity;
}

[[nodiscard]] inline Core::ECS::EntityID CreateTintedModelEntity(
    Core::ECS::World& world,
    Core::Alloc::GlobalArena& arena,
    const SmokeModelRef& model,
    const SmokeMaterialRef& material,
    const AStringView materialInterfacePath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale
){
    bool tintApplied = false;
    return CreateTintedModelEntity(
        world,
        arena,
        model,
        material,
        materialInterfacePath,
        colorTint,
        position,
        scale,
        tintApplied
    );
}

[[nodiscard]] inline Core::ECS::EntityID FindSpawnedModelObject(
    Core::ECS::World& world,
    const Core::ECS::EntityID owner,
    const Name objectName,
    const u32 objectKind
){
    Core::ECS::EntityID result = Core::ECS::ENTITY_ID_INVALID;
    world.view<Impl::ModelObjectComponent>().each(
        [&](const Core::ECS::EntityID entity, Impl::ModelObjectComponent& object){
            if(result.valid())
                return;
            if(object.owner == owner && object.object == objectName && object.kind == objectKind)
                result = entity;
        }
    );
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

