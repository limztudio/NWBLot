// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#ifndef NWB_TESTS_SMOKE_SKINNED_SCENE_HELPERS_H
#define NWB_TESTS_SMOKE_SKINNED_SCENE_HELPERS_H

#include "smoke_scene_helpers.h"

#include <impl/ecs_mesh/skinning/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/model_renderer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB{
namespace Tests{
namespace Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void AddSmokeSkinnedRenderSystems(
    Core::ECS::World& world,
    ProjectRuntimeContext& context
){
    auto& meshSystem = world.addSystem<Impl::MeshSystem>(world);
    auto& rendererSystem = world.addSystem<Impl::RendererSystem>(
        world,
        context.graphics,
        context.assetManager,
        context.shaderPathResolver
    );
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
        meshSystem,
        context.shaderPathResolver
    );

    context.graphics.addRenderPassToBack(meshSkinningSystem);
    context.graphics.addRenderPassToBack(rendererSystem);
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

    auto* rendererSystem = world->getSystem<Impl::RendererSystem>();
    if(rendererSystem)
        context.graphics.removeRenderPass(*rendererSystem);

    FinishDestroyingSmokeWorld(context, world);
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


}
}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
