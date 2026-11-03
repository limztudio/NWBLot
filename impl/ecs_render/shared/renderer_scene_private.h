// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/caustic/settings.adhd.h>
#include <impl/ecs_render/shadow/types.adhd.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/assets/graphics/scene/binding_slots.h>
#include <impl/ecs_scene/module.h>

#include <core/ecs/world.h>

#include <global/algorithm.h>
#include <global/simd/vector.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(RayTracingLightingClassificationInput) == sizeof(u32));
static_assert(sizeof(RayTracingLightingClassification) == sizeof(u32) * 2u);
static_assert(sizeof(DeferredBindlessResourceSlots) == sizeof(u32) * 44u, "Deferred bindless slots must match eleven std140 uint4 lanes");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u32 ResolveSceneLights(Core::ECS::World& world, const Span<SceneLightGpuData> outLights, const Span<f32> outCausticImportance)noexcept{
    const usize maxLightCount = Min(outLights.size(), outCausticImportance.size());
    if(maxLightCount == 0u)
        return 0u;

    const u32 capacity = static_cast<u32>(Min<usize>(maxLightCount, NWB_SCENE_MAX_LIGHTS));

    const SIMDMatrix defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();
    Array<NWB::Impl::Scene::SceneLight, NWB_SCENE_MAX_LIGHTS> sceneLights{};
    Array<f32, NWB_SCENE_MAX_LIGHTS> shadowImportance{};
    const usize gatheredCount = NWB::Impl::Scene::GatherSceneLights(world, defaultBasis.v[2], Span(sceneLights.data(), capacity));
    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, NWB::Impl::Scene::s_CameraFallbackAspectRatio);
    const Float4 cameraPositionData = cameraView.valid() ? cameraView.transform->position : Float4(0.f, 0.f, 0.f, 0.f);

    for(usize i = 0u; i < gatheredCount; ++i){
        const NWB::Impl::Scene::SceneLight& src = sceneLights[i];
        SceneLightGpuData& dst = outLights[i];
        dst = MakeSceneLightGpuData(
            src.position,
            src.direction,
            src.colorIntensity,
            src.range,
            static_cast<f32>(src.type),
            s_ShadowSlotUnassigned,
            src.enableCaustics ? s_ShadowSlotUnassigned : s_CausticSlotDisabled,
            src.angularRadius,
            src.sourceRadius
        );
        outCausticImportance[i] = SceneLightRadiantImportance(src.colorIntensity);
        shadowImportance[i] = ShadowSlotImportanceData(dst.colorIntensity, dst.params, dst.position, cameraPositionData);
    }

    // Importance-ranked shadow slots: most important lights get the pool (slot -> params.z; rest stay lit).
    // Selection over <= NWB_SCENE_MAX_LIGHTS lights stays cheap.
    const u32 lightCount = static_cast<u32>(gatheredCount);
    const u32 slotCount = (lightCount < NWB_SCENE_SHADOW_SLOT_COUNT) ? lightCount : NWB_SCENE_SHADOW_SLOT_COUNT;
    for(u32 slot = 0u; slot < slotCount; ++slot){
        const u32 bestIndex = SelectNextShadowSlotLightIndex(Span<const SceneLightGpuData>(outLights.data(), lightCount), Span<const f32>(shadowImportance.data(), lightCount));
        if(bestIndex >= lightCount)
            break;
        outLights[bestIndex].params.z = static_cast<f32>(slot);
    }

    return static_cast<u32>(gatheredCount);
}

[[nodiscard]] inline SceneShadingGpuData ResolveSceneShadingState(const f32 fallbackAspectRatio, const u32 lightCount, Core::ECS::World& world)noexcept{
    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid())
        return MakeSceneShadingGpuData(cameraView.transform->position, lightCount);

    const SIMDMatrix defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();
    Float4 defaultCameraPosition;
    StoreFloat(defaultBasis.v[3], defaultCameraPosition);
    return MakeSceneShadingGpuData(defaultCameraPosition, lightCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

