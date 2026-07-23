// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#ifndef NWB_TESTS_SMOKE_SKINNED_SCENE_HELPERS_H
#define NWB_TESTS_SMOKE_SKINNED_SCENE_HELPERS_H

#include "smoke_scene_helpers.h"

#include <core/common/log.h>
#include <impl/assets_model/asset.h>
#include <impl/ecs_mesh/skinning/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/mesh/model_renderer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB{
namespace Tests{
namespace Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void AddSmokeSkinnedRenderSystems(
    Core::ECS::World& world,
    ProjectRuntimeContext& context
){
    const SmokeRenderSystems systems = CreateSmokeRenderSystems(world, context);
    auto& modelSystem = world.addSystem<Impl::ModelSystem>(
        world,
        context.assetManager,
        Impl::CreateModelObjectRendererHooks()
    );
    static_cast<void>(modelSystem);
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

    auto* meshSkinningSystem = world->getSystem<Impl::MeshSkinningSystem>();
    if(meshSkinningSystem)
        context.graphics.removeRenderPass(*meshSkinningSystem);

    RemoveSmokeRendererSystem(context, *world);
    FinishDestroyingSmokeWorld(context, world);
}

inline void SyncSmokeModelRuntimes(Core::ECS::World& world){
    auto* modelSystem = world.getSystem<Impl::ModelSystem>();
    if(!modelSystem){
        NWB_LOGGER_ERROR(NWB_TEXT("Smoke: failed to sync model runtimes because the model system is missing"));
        return;
    }

    modelSystem->syncModelRuntimes();
}

[[nodiscard]] inline Core::ECS::EntityID CreateTintedModelEntity(
    Core::ECS::World& world,
    Core::Alloc::GlobalArena& arena,
    const AStringView modelPath,
    const AStringView materialPath,
    const AStringView materialInterfacePath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    bool* const outTintApplied = nullptr
){
    if(outTintApplied)
        *outTintApplied = false;

    Core::Assets::AssetRef<Impl::Model> model;
    model.virtualPath = Name(modelPath);
    const SmokeTintedEntitySetup setup = CreateSmokeTintedEntity(
        world,
        arena,
        materialPath,
        materialInterfacePath,
        position,
        scale
    );

    auto& modelComponent = world.addComponent<Impl::ModelComponent>(setup.entity);
    modelComponent.model = model;

    const bool tintApplied = ApplySmokeMaterialTint(world, setup, colorTint);
    if(outTintApplied)
        *outTintApplied = tintApplied;

    return setup.entity;
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


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

