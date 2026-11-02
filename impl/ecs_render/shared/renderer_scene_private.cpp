// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_scene_private.h"

#include <impl/ecs_render/caustic/settings.adhd.h>
#include <impl/ecs_render/shadow/types.adhd.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 ResolveSceneLights(Core::ECS::World& world, const Span<SceneLightGpuData> outLights, const Span<f32> outCausticImportance)noexcept{
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
        dst.position = src.position;
        dst.direction = src.direction;
        dst.colorIntensity = src.colorIntensity;
        dst.params = Float4(src.range, static_cast<f32>(src.type), s_ShadowSlotUnassigned, src.enableCaustics ? s_ShadowSlotUnassigned : s_CausticSlotDisabled); // z = shadow slot; w = caustic slot, or disabled when the light did not opt in
        dst.params2 = Float4(src.angularRadius, src.sourceRadius, 0.f, 0.f); // soft-shadow source size
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

SceneShadingGpuData ResolveSceneShadingState(const f32 fallbackAspectRatio, const u32 lightCount, Core::ECS::World& world)noexcept{
    SceneShadingGpuData state;
    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid()){
        state.cameraPositionLightCount = cameraView.transform->position;
        state.cameraPositionLightCount.w = static_cast<f32>(lightCount);
        return state;
    }

    const SIMDMatrix defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();
    StoreFloat(VectorSetW(defaultBasis.v[3], static_cast<f32>(lightCount)), state.cameraPositionLightCount);
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

