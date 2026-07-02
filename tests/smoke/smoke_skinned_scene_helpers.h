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
    context.frameGraphRegistry.registerContributor(rendererSystem);
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
    if(rendererSystem){
        context.frameGraphRegistry.unregisterContributor(*rendererSystem);
        context.graphics.removeRenderPass(*rendererSystem);
    }

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
    Core::Assets::AssetRef<Impl::Material> material;
    material.virtualPath = Name(materialPath);

    auto entity = world.createEntity();
    auto& transform = entity.addComponent<Impl::Scene::TransformComponent>();
    transform.position = position;
    transform.scale = scale;

    auto& modelComponent = entity.addComponent<Impl::ModelComponent>();
    modelComponent.model = model;

    auto& renderer = entity.addComponent<Impl::RendererComponent>();
    renderer.material = material;

    const Name materialInterface(materialInterfacePath);
    entity.addComponent<Impl::MaterialInstanceComponent>(arena, materialInterface);
    const bool tintApplied = Impl::SetMaterialMutableHalf4(
        world,
        entity.id(),
        materialInterface,
        "runtime.color_tint",
        colorTint
    );
    if(outTintApplied)
        *outTintApplied = tintApplied;

    return entity.id();
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

