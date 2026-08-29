// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#ifndef NWB_TESTS_SMOKE_SCENE_HELPERS_H
#define NWB_TESTS_SMOKE_SCENE_HELPERS_H

#include <loader/project_entry.h>

#include "smoke_environment.h"

#include <core/assets/ref.h>
#include <core/ecs/entity.h>
#include <core/ecs/world.h>
#include <core/graphics/module.h>
#include <core/telemetry/frame_graph_registry.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_mesh/asset.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_scene/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB{
namespace Tests{
namespace Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SmokeMaterialRef = Core::Assets::AssetRef<Impl::Material>;
using SmokeMeshRef = Core::Assets::AssetRef<Impl::Mesh>;


struct SmokeTintedEntitySetup{
    Core::ECS::EntityID entity;
    Name materialInterface;
};

[[nodiscard]] inline SmokeTintedEntitySetup CreateSmokeTintedEntity(
    Core::ECS::World& world,
    Core::Alloc::GlobalArena& arena,
    const SmokeMaterialRef& material,
    const AStringView materialInterfacePath,
    const Float4& position,
    const Float4& scale
){
    auto entity = world.createEntity();
    auto& transform = entity.addComponent<Impl::Scene::TransformComponent>();
    transform.position = position;
    transform.scale = scale;

    auto& renderer = entity.addComponent<Impl::RendererComponent>();
    renderer.material = material;

    const Name materialInterface(materialInterfacePath);
    entity.addComponent<Impl::MaterialInstanceComponent>(arena, materialInterface);
    return { entity.id(), materialInterface };
}

[[nodiscard]] inline bool ApplySmokeMaterialTint(
    Core::ECS::World& world,
    const SmokeTintedEntitySetup& setup,
    const Float4& colorTint,
    const AStringView tintParameterPath = "runtime.color_tint"
){
    return Impl::SetMaterialMutableHalf4(
        world,
        setup.entity,
        setup.materialInterface,
        tintParameterPath,
        colorTint
    );
}

[[nodiscard]] inline Core::ECS::EntityID CreateTintedStaticMeshEntity(
    Core::ECS::World& world,
    Core::Alloc::GlobalArena& arena,
    const SmokeMeshRef& mesh,
    const SmokeMaterialRef& material,
    const AStringView materialInterfacePath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    const AStringView tintParameterPath = "runtime.color_tint"
){
    const SmokeTintedEntitySetup setup = CreateSmokeTintedEntity(
        world,
        arena,
        material,
        materialInterfacePath,
        position,
        scale
    );

    auto& meshComponent = world.entity(setup.entity).addComponent<Impl::MeshComponent>();
    meshComponent.mesh = mesh;

    if(!ApplySmokeMaterialTint(world, setup, colorTint, tintParameterPath))
        return Core::ECS::ENTITY_ID_INVALID;

    return setup.entity;
}

struct SmokeRenderSystems{
    Impl::MeshSystem& mesh;
    Impl::RendererSystem& renderer;
};

[[nodiscard]] inline SmokeRenderSystems CreateSmokeRenderSystems(
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
    return { meshSystem, rendererSystem };
}

inline Impl::RendererSystem& AddSmokeRenderSystems(
    Core::ECS::World& world,
    ProjectRuntimeContext& context
){
    const SmokeRenderSystems systems = CreateSmokeRenderSystems(world, context);

    context.graphics.addRenderPassToBack(systems.renderer);
    context.frameGraphRegistry.registerContributor(systems.renderer);
    return systems.renderer;
}

inline void FinishDestroyingSmokeWorld(
    ProjectRuntimeContext& context,
    NotNullUniquePtr<Core::ECS::World>& world
){
    context.graphics.waitAllJobs();
    context.graphics.waitForIdle();

    world->clear();
    world.owner().reset();
}

inline void RemoveSmokeRendererSystem(
    ProjectRuntimeContext& context,
    Core::ECS::World& world
){
    auto* rendererSystem = world.getSystem<Impl::RendererSystem>();
    NWB_ASSERT(rendererSystem);
    context.frameGraphRegistry.unregisterContributor(*rendererSystem);
    context.graphics.removeRenderPass(*rendererSystem);
}

inline void DestroySmokeRenderWorld(
    ProjectRuntimeContext& context,
    NotNullUniquePtr<Core::ECS::World>& world
){
    if(!world.owner())
        return;

    RemoveSmokeRendererSystem(context, *world);
    FinishDestroyingSmokeWorld(context, world);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

