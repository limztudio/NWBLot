// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_environment.h"
#include "smoke_project_helpers.h"

#include <loader/project_entry.h>

#include <impl/assets_material/asset.h>
#include <impl/assets_mesh/asset.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_scene/module.h>

#include <core/alloc/scratch.h>
#include <core/assets/ref.h>
#include <core/common/log.h>
#include <core/ecs/entity.h>
#include <core/ecs/world.h>
#include <core/graphics/runtime/runtime.h>
#include <core/telemetry/frame_graph_registry.h>

#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene defaults are applied before explicit smoke environment overrides.
struct SmokeRenderQualitySettings{
    Impl::CausticQualitySettings caustic;
    Impl::SurfelGiQualitySettings surfelGi;
    Impl::ShadowQualitySettings shadow;
    Impl::SoftwareShadowSettings softwareShadow;
};

[[nodiscard]] inline b8 ApplyCausticQualitySmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena, const Impl::CausticQualitySettings& baseSettings = {}){
    Impl::CausticQualitySettings settings = baseSettings;
    const auto value = ReadSmokeEnvironmentText(arena, "NWB_CAUSTIC_PHOTON_GRID_DIVISOR");
    if(value){
        const auto parsed = ParseU64(AStringView(value->data(), value->size()));
        if(!parsed || *parsed > Limit<u32>::s_Max)
            return false;
        settings.photonGridDivisor = static_cast<u32>(*parsed);
    }
    if(!renderer.setCausticQualitySettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticQualitySmoke: requested photon_grid_divisor={}"), settings.photonGridDivisor);
    return true;
}

[[nodiscard]] inline b8 ApplySurfelGiQualitySmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena, const Impl::SurfelGiQualitySettings& baseSettings = {}){
    Impl::SurfelGiQualitySettings settings = baseSettings;
    const auto value = ReadSmokeEnvironmentText(arena, "NWB_SURFEL_GI_RESOLVE_RESOLUTION");
    if(value){
        const AStringView resolution(value->data(), value->size());
        if(resolution == "half")
            settings.resolveResolution = Impl::SurfelGiResolveResolution::Half;
        else if(resolution == "quarter")
            settings.resolveResolution = Impl::SurfelGiResolveResolution::Quarter;
        else
            return false;
    }
    if(!renderer.setSurfelGiQualitySettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SurfelGiQualitySmoke: requested resolve_factor={}"), static_cast<u32>(settings.resolveResolution));
    return true;
}

[[nodiscard]] inline b8 ApplyShadowQualitySmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena, const Impl::ShadowQualitySettings& baseSettings = {}){
    Impl::ShadowQualitySettings settings = baseSettings;
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SHADOW_TRANSPARENT_SAMPLING")){
        const AStringView sampling(value->data(), value->size());
        if(sampling == "reference_three")
            settings.transparentSampling = Impl::TransparentShadowSampling::ReferenceThree;
        else if(sampling == "temporal_one")
            settings.transparentSampling = Impl::TransparentShadowSampling::TemporalOne;
        else
            return false;
    }
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SHADOW_RECEIVER_RESOLUTION")){
        const AStringView resolution(value->data(), value->size());
        if(resolution == "half")
            settings.receiverResolution = Impl::ShadowReceiverResolution::Half;
        else if(resolution == "quarter")
            settings.receiverResolution = Impl::ShadowReceiverResolution::Quarter;
        else
            return false;
    }
    if(!renderer.setShadowQualitySettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowQualitySmoke: requested transparent_sampling={} receiver_factor={}"), static_cast<u32>(settings.transparentSampling), static_cast<u32>(settings.receiverResolution));
    return true;
}

[[nodiscard]] inline b8 ApplySoftwareShadowSmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena, const Impl::SoftwareShadowSettings& baseSettings = {}){
    Impl::SoftwareShadowSettings settings = baseSettings;
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SOFTWARE_SHADOW_BACKEND")){
        const AStringView mode(value->data(), value->size());
        if(mode == "automatic")
            settings.backend = Impl::SoftwareShadowBackend::Automatic;
        else if(mode == "trace")
            settings.backend = Impl::SoftwareShadowBackend::SoftwareTrace;
        else if(mode == "light_space")
            settings.backend = Impl::SoftwareShadowBackend::LightSpace;
        else
            return false;
    }
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SOFTWARE_SHADOW_COVERAGE")){
        const AStringView coverage(value->data(), value->size());
        if(coverage == "reference")
            settings.coverage = Impl::SoftwareShadowCoverage::Reference;
        else if(coverage == "fitted_volume")
            settings.coverage = Impl::SoftwareShadowCoverage::FittedVolume;
        else
            return false;
    }
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH")){
        const AStringView search(value->data(), value->size());
        if(search == "reference_grid9")
            settings.blockerSearch = Impl::SoftwareShadowBlockerSearch::ReferenceGrid9;
        else if(search == "compact_cross5")
            settings.blockerSearch = Impl::SoftwareShadowBlockerSearch::CompactCross5;
        else if(search == "center1")
            settings.blockerSearch = Impl::SoftwareShadowBlockerSearch::Center1;
        else
            return false;
    }
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE")){
        const AStringView cadence(value->data(), value->size());
        if(cadence == "every_frame")
            settings.captureCadence = Impl::SoftwareShadowCaptureCadence::EveryFrame;
        else if(cadence == "reuse_one_frame")
            settings.captureCadence = Impl::SoftwareShadowCaptureCadence::ReuseOneFrame;
        else if(cadence == "reuse_two_frames")
            settings.captureCadence = Impl::SoftwareShadowCaptureCadence::ReuseTwoFrames;
        else
            return false;
    }
    struct ResolutionOverride{ AStringView name; u32& destination; };
    const Array<ResolutionOverride, 2u> overrides = {
        ResolutionOverride{ "NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION", settings.directionalResolution },
        ResolutionOverride{ "NWB_SOFTWARE_SHADOW_POINT_RESOLUTION", settings.pointResolution },
    };
    for(const auto& setting : overrides){
        const auto value = ReadSmokeEnvironmentText(arena, setting.name);
        if(!value)
            continue;
        const auto parsed = ParseU64(AStringView(value->data(), value->size()));
        if(!parsed || *parsed > Limit<u32>::s_Max)
            return false;
        setting.destination = static_cast<u32>(*parsed);
    }
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SOFTWARE_SHADOW_BUDGET_MIB")){
        const auto parsed = ParseU64(AStringView(value->data(), value->size()));
        if(!parsed || *parsed > Limit<u32>::s_Max / (1024u * 1024u))
            return false;
        settings.memoryBudgetBytes = *parsed * 1024u * 1024u;
    }
    if(!renderer.setSoftwareShadowSettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SoftwareShadowSmoke: requested backend={} directional_resolution={} point_resolution={} budget_bytes={} coverage={} blocker_search={} capture_cadence={}")
        , static_cast<u32>(settings.backend)
        , settings.directionalResolution
        , settings.pointResolution
        , settings.memoryBudgetBytes
        , static_cast<u32>(settings.coverage)
        , static_cast<u32>(settings.blockerSearch)
        , static_cast<u32>(settings.captureCadence)
    );
    return true;
}

[[nodiscard]] inline b8 ApplyReflectionQualitySmokeSettings(const Impl::ReflectionSettings& baseSettings, Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena){
    Impl::ReflectionSettings settings = baseSettings;
    const auto value = ReadSmokeEnvironmentText(arena, "NWB_REFLECTION_SCREEN_STEPS");
    if(value){
        const auto parsed = ParseU64(AStringView(value->data(), value->size()));
        if(!parsed || *parsed > Limit<u32>::s_Max)
            return false;
        settings.screenMaxSteps = static_cast<u32>(*parsed);
    }
    if(!renderer.setReflectionSettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionQualitySmoke: requested screen_max_steps={}"), settings.screenMaxSteps);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SmokeMaterialRef = Core::Assets::AssetRef<Impl::Material>;
using SmokeMeshRef = Core::Assets::AssetRef<Impl::Mesh>;

struct SmokeTintedEntitySetup{
    Name materialInterface;
    Core::ECS::EntityID entity;
};

struct SmokeRenderSystems{
    Impl::MeshSystem& mesh;
    Impl::RendererSystem& renderer;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SmokeTintedEntitySetup CreateSmokeTintedEntity(
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

[[nodiscard]] inline b8 ApplySmokeMaterialTint(
    const SmokeTintedEntitySetup& setup,
    const Float4& colorTint,
    const AStringView tintParameterPath,
    Core::ECS::World& world
){
    return Impl::SetMaterialMutableHalf4(world, setup.entity, setup.materialInterface, tintParameterPath, colorTint);
}

[[nodiscard]] inline Core::ECS::EntityID CreateTintedStaticMeshEntity(
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

[[nodiscard]] inline Core::ECS::EntityID CreateTintedStaticMeshEntity(
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

[[nodiscard]] inline SmokeRenderSystems CreateSmokeRenderSystems(Core::ECS::World& world, ProjectRuntimeContext& context, const SmokeRenderQualitySettings& baseSettings = {}){
    Core::Alloc::ScratchArena scratchArena(s_SmokeEnvironmentArena);
    auto& meshSystem = world.addSystem<Impl::MeshSystem>(world);
    auto& rendererSystem = world.addSystem<Impl::RendererSystem>(world, context.graphics, context.assetManager, context.shaderPathResolver);
    NWB_FATAL_ASSERT_MSG(ApplyCausticQualitySmokeSettings(rendererSystem, scratchArena, baseSettings.caustic), NWB_TEXT("Invalid caustic quality smoke settings"));
    NWB_FATAL_ASSERT_MSG(ApplySurfelGiQualitySmokeSettings(rendererSystem, scratchArena, baseSettings.surfelGi), NWB_TEXT("Invalid surfel GI quality smoke settings"));
    NWB_FATAL_ASSERT_MSG(ApplySoftwareShadowSmokeSettings(rendererSystem, scratchArena, baseSettings.softwareShadow), NWB_TEXT("Invalid software shadow smoke settings"));
    NWB_FATAL_ASSERT_MSG(ApplyShadowQualitySmokeSettings(rendererSystem, scratchArena, baseSettings.shadow), NWB_TEXT("Invalid shadow quality smoke settings"));
    return { meshSystem, rendererSystem };
}

inline Impl::RendererSystem& AddSmokeRenderSystems(Core::ECS::World& world, ProjectRuntimeContext& context){
    const SmokeRenderSystems systems = CreateSmokeRenderSystems(world, context);

    context.graphics.addRenderPassToBack(systems.renderer);
    context.frameGraphRegistry.registerContributor(systems.renderer);
    return systems.renderer;
}

[[nodiscard]] inline NotNullUniquePtr<Core::ECS::World> CreateSmokeRenderWorldOrDie(const TStringView projectName, ProjectRuntimeContext& context){
    auto world = CreateSmokeWorldOrDie(projectName, context);
    AddSmokeRenderSystems(*world, context);
    return world;
}

inline void FinishDestroyingSmokeWorld(ProjectRuntimeContext& context, NotNullUniquePtr<Core::ECS::World>& world){
    const b8 deviceIdle = context.graphics.waitForIdle();
    NWB_FATAL_ASSERT_MSG(deviceIdle || context.graphics.isDeviceLost(), NWB_TEXT("Smoke-world destruction requires either a completed device join or terminal device loss"));

    world->clear();
    world.owner().reset();
}

inline void RemoveSmokeRendererSystem(ProjectRuntimeContext& context, Core::ECS::World& world){
    Impl::RendererSystem& rendererSystem = *MakeNotNull(world.getSystem<Impl::RendererSystem>());
    context.frameGraphRegistry.unregisterContributor(rendererSystem);
    context.graphics.removeRenderPass(rendererSystem);
}

inline void DestroySmokeRenderWorld(ProjectRuntimeContext& context, NotNullUniquePtr<Core::ECS::World>& world){
    if(!world.owner())
        return;

    RemoveSmokeRendererSystem(context, *world);
    FinishDestroyingSmokeWorld(context, world);
}

inline void DestroyCapturedSmokeRenderWorld(ProjectRuntimeContext& context, NotNullUniquePtr<Core::ECS::World>& world, UniquePtr<FramebufferCapture>& capture){
    DestroySmokeRenderWorld(context, world);
    if(capture){
        capture->stop();
        capture.reset();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
