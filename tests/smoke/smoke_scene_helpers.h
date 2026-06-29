// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#ifndef NWB_TESTS_SMOKE_SCENE_HELPERS_H
#define NWB_TESTS_SMOKE_SCENE_HELPERS_H

#include <loader/project_entry.h>

#include <core/assets/ref.h>
#include <core/ecs/world.h>
#include <core/graphics/module.h>
#include <core/telemetry/frame_graph_registry.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_mesh/asset.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/material_instance.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_scene/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB{
namespace Tests{
namespace Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Core::ECS::EntityID CreateTintedStaticMeshEntity(
    Core::ECS::World& world,
    Core::Alloc::GlobalArena& arena,
    const AStringView meshPath,
    const AStringView materialPath,
    const AStringView materialInterfacePath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale
){
    Core::Assets::AssetRef<Impl::Mesh> mesh;
    mesh.virtualPath = Name(meshPath);
    Core::Assets::AssetRef<Impl::Material> material;
    material.virtualPath = Name(materialPath);

    auto entity = world.createEntity();
    auto& transform = entity.addComponent<Impl::Scene::TransformComponent>();
    transform.position = position;
    transform.scale = scale;

    auto& meshComponent = entity.addComponent<Impl::MeshComponent>();
    meshComponent.mesh = mesh;

    auto& renderer = entity.addComponent<Impl::RendererComponent>();
    renderer.material = material;

    const Name materialInterface(materialInterfacePath);
    entity.addComponent<Impl::MaterialInstanceComponent>(arena, materialInterface);
    if(!Impl::SetMaterialMutableHalf4(
        world,
        entity.id(),
        materialInterface,
        "runtime.color_tint",
        colorTint
    ))
        return Core::ECS::ENTITY_ID_INVALID;

    return entity.id();
}

inline Impl::RendererSystem& AddSmokeRenderSystems(
    Core::ECS::World& world,
    ProjectRuntimeContext& context
){
    world.addSystem<Impl::MeshSystem>(world);
    auto& rendererSystem = world.addSystem<Impl::RendererSystem>(
        world,
        context.graphics,
        context.assetManager,
        context.shaderPathResolver
    );

    context.graphics.addRenderPassToBack(rendererSystem);
    context.frameGraphRegistry.registerContributor(rendererSystem);
    return rendererSystem;
}

inline void FinishDestroyingSmokeWorld(
    ProjectRuntimeContext& context,
    NotNullUniquePtr<Core::ECS::World>& world
){
    context.graphics.waitAllJobs();
    if(auto* device = context.graphics.getDevice())
        device->waitForIdle();

    world->clear();
    world.owner().reset();
}

inline void DestroySmokeRenderWorld(
    ProjectRuntimeContext& context,
    NotNullUniquePtr<Core::ECS::World>& world
){
    if(!world.owner())
        return;

    auto* rendererSystem = world->getSystem<Impl::RendererSystem>();
    if(rendererSystem){
        context.frameGraphRegistry.unregisterContributor(*rendererSystem);
        context.graphics.removeRenderPass(*rendererSystem);
    }

    FinishDestroyingSmokeWorld(context, world);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}
}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

