// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SmokeTintedEntitySetup CreateSmokeTintedEntity(
    const SmokeMaterialRef& material,
    const NameText<char> materialInterfacePath,
    const Float4& position,
    const Float4& scale,
    Core::ECS::World& world
){
    auto entity = world.createEntity();
    auto& transform = entity.addComponent<Impl::Scene::TransformComponent>();
    transform.position = position;
    transform.scale = scale;

    auto& renderer = entity.addComponent<Impl::RendererComponent>();
    renderer.material = material;

    const Name materialInterface(materialInterfacePath);
    entity.addComponent<Impl::MaterialInstanceComponent>(materialInterface);
    return { .materialInterface = materialInterface, .entity = entity.id() };
}

b8 ApplySmokeMaterialTint(
    const SmokeTintedEntitySetup& setup,
    const Float4& colorTint,
    const AStringView tintParameterPath,
    Core::ECS::World& world
){
    return Impl::SetMaterialMutableHalf4(world, setup.entity, setup.materialInterface, tintParameterPath, colorTint);
}

Core::ECS::EntityID CreateTintedStaticMeshEntity(
    const SmokeMeshRef& mesh,
    const SmokeMaterialRef& material,
    const NameText<char> materialInterfacePath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    const AStringView tintParameterPath,
    Core::ECS::World& world
){
    const SmokeTintedEntitySetup setup = CreateSmokeTintedEntity(material, materialInterfacePath, position, scale, world);

    auto& meshComponent = world.entity(setup.entity).addComponent<Impl::MeshComponent>();
    meshComponent.mesh = mesh;

    if(!ApplySmokeMaterialTint(setup, colorTint, tintParameterPath, world))
        return Core::ECS::s_InvalidEntityId;

    return setup.entity;
}

Core::ECS::EntityID CreateTintedStaticMeshEntity(
    const SmokeMeshRef& mesh,
    const SmokeMaterialRef& material,
    const NameText<char> materialInterfacePath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    Core::ECS::World& world
){
    return CreateTintedStaticMeshEntity(mesh, material, materialInterfacePath, colorTint, position, scale, "runtime.color_tint", world);
}

SmokeRenderSystems CreateSmokeRenderSystems(Core::ECS::World& world, ProjectRuntimeContext& context, const SmokeRenderQualitySettings& baseSettings){
    Core::Alloc::ScratchArena scratchArena(s_SmokeEnvironmentArena);
    auto& meshSystem = world.addSystem<Impl::MeshSystem>(world);
    auto& rendererSystem = world.addSystem<Impl::RendererSystem>(world, context.graphics, context.assetManager, context.shaderPathResolver);
    NWB_FATAL_ASSERT_MSG(ApplyCausticQualitySmokeSettings(rendererSystem, scratchArena, baseSettings.caustic), NWB_TEXT("Invalid caustic quality smoke settings"));
    NWB_FATAL_ASSERT_MSG(ApplySurfelGiQualitySmokeSettings(rendererSystem, scratchArena, baseSettings.surfelGi), NWB_TEXT("Invalid surfel GI quality smoke settings"));
    NWB_FATAL_ASSERT_MSG(ApplySoftwareShadowSmokeSettings(rendererSystem, scratchArena, baseSettings.softwareShadow), NWB_TEXT("Invalid software shadow smoke settings"));
    NWB_FATAL_ASSERT_MSG(ApplyShadowQualitySmokeSettings(rendererSystem, scratchArena, baseSettings.shadow), NWB_TEXT("Invalid shadow quality smoke settings"));
    return { meshSystem, rendererSystem };
}

Impl::RendererSystem& AddSmokeRenderSystems(Core::ECS::World& world, ProjectRuntimeContext& context){
    const SmokeRenderSystems systems = CreateSmokeRenderSystems(world, context);

    context.graphics.addRenderPassToBack(systems.renderer);
    context.frameGraphRegistry.registerContributor(systems.renderer);
    return systems.renderer;
}

NotNullUniquePtr<Core::ECS::World> CreateSmokeRenderWorldOrDie(const TStringView projectName, ProjectRuntimeContext& context){
    auto world = CreateSmokeWorldOrDie(projectName, context);
    AddSmokeRenderSystems(*world, context);
    return world;
}

void FinishDestroyingSmokeWorld(ProjectRuntimeContext& context, NotNullUniquePtr<Core::ECS::World>& world){
    const b8 deviceIdle = context.graphics.waitForIdle();
    NWB_FATAL_ASSERT_MSG(deviceIdle || context.graphics.isDeviceLost(), NWB_TEXT("Smoke-world destruction requires either a completed device join or terminal device loss"));

    world->clear();
    world.owner().reset();
}

void RemoveSmokeRendererSystem(ProjectRuntimeContext& context, Core::ECS::World& world){
    Impl::RendererSystem& rendererSystem = *MakeNotNull(world.getSystem<Impl::RendererSystem>());
    context.frameGraphRegistry.unregisterContributor(rendererSystem);
    context.graphics.removeRenderPass(rendererSystem);
}

void DestroySmokeRenderWorld(ProjectRuntimeContext& context, NotNullUniquePtr<Core::ECS::World>& world){
    if(!world.owner())
        return;

    RemoveSmokeRendererSystem(context, *world);
    FinishDestroyingSmokeWorld(context, world);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

