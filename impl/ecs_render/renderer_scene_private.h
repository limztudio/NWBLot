// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_push_constants_private.h"

#include <core/ecs/world.h>
#include <impl/ecs_scene/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE f32 ResolveExtentAspectRatio(const u32 width, const u32 height){
    if(width != 0u && height != 0u)
        return static_cast<f32>(width) / static_cast<f32>(height);

    return 1.0f;
}

NWB_INLINE f32 ResolveFramebufferAspectRatio(const Core::FramebufferInfoEx& framebufferInfo){
    return ResolveExtentAspectRatio(framebufferInfo.width, framebufferInfo.height);
}

// Importance heuristic for the bounded shadow-slot pool: radiant power (luminance * intensity) weighted by
// screen coverage. Directional lights cover the whole frame (and always outrank comparable local lights);
// local lights are weighted by their influence range relative to the camera distance (a squared proxy --
// monotonic, no sqrt). Higher = more worth a scarce shadow slot.
inline f32 ShadowSlotImportance(const SceneLightGpuData& light, const Float4& cameraPosition){
    const f32 luminance = 0.2126f * light.colorIntensity.x + 0.7152f * light.colorIntensity.y + 0.0722f * light.colorIntensity.z;
    const f32 intensity = light.colorIntensity.w > 0.f ? light.colorIntensity.w : 0.f;
    const f32 radiantImportance = luminance * intensity;

    if(light.params.y < 0.5f)
        return radiantImportance * 4.f + 1.f;

    const f32 dx = light.position.x - cameraPosition.x;
    const f32 dy = light.position.y - cameraPosition.y;
    const f32 dz = light.position.z - cameraPosition.z;
    const f32 distanceSquared = dx * dx + dy * dy + dz * dz;
    const f32 range = light.params.x > 0.f ? light.params.x : 0.f;
    const f32 coverageSquared = (distanceSquared > 1e-4f) ? ((range * range) / distanceSquared) : 1.f;
    return radiantImportance * (coverageSquared < 1.f ? coverageSquared : 1.f);
}

inline u32 ResolveSceneLights(Core::ECS::World& world, SceneLightGpuData* outLights, const u32 maxLights){
    if(maxLights == 0u)
        return 0u;

    u32 capacity = maxLights;
    if(capacity > NWB_SCENE_MAX_LIGHTS)
        capacity = NWB_SCENE_MAX_LIGHTS;

    const NWB::Impl::Scene::SceneViewBasis defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();
    NWB::Impl::Scene::SceneLight sceneLights[NWB_SCENE_MAX_LIGHTS];
    const usize gatheredCount = NWB::Impl::Scene::GatherSceneLights(world, defaultBasis, sceneLights, capacity);

    for(usize i = 0u; i < gatheredCount; ++i){
        const NWB::Impl::Scene::SceneLight& src = sceneLights[i];
        SceneLightGpuData& dst = outLights[i];
        dst.position = src.position;
        dst.direction = src.direction;
        dst.colorIntensity = src.colorIntensity;
        dst.params = Float4(src.range, static_cast<f32>(src.type), -1.f, 0.f); // z = shadow slot, assigned below
    }

    // Importance-ranked shadow-slot allocator: hand the bounded pool of NWB_SCENE_SHADOW_SLOT_COUNT slots to
    // the most important lights this frame (slot index -> params.z; lights that miss out keep -1 and stay
    // fully lit). A simple K-pass selection over <= NWB_SCENE_MAX_LIGHTS lights is trivially cheap.
    const u32 lightCount = static_cast<u32>(gatheredCount);
    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, 1.0f);
    Float4 cameraPosition(0.f, 0.f, 0.f, 0.f);
    if(cameraView.valid())
        StoreFloat(LoadFloat(cameraView.transform->position), &cameraPosition);

    const u32 slotCount = (lightCount < NWB_SCENE_SHADOW_SLOT_COUNT) ? lightCount : NWB_SCENE_SHADOW_SLOT_COUNT;
    for(u32 slot = 0u; slot < slotCount; ++slot){
        f32 bestImportance = -1.f;
        u32 bestIndex = lightCount;
        for(u32 i = 0u; i < lightCount; ++i){
            if(outLights[i].params.z >= 0.f)
                continue;
            const f32 importance = ShadowSlotImportance(outLights[i], cameraPosition);
            if(importance > bestImportance){
                bestImportance = importance;
                bestIndex = i;
            }
        }
        if(bestIndex >= lightCount)
            break;
        outLights[bestIndex].params.z = static_cast<f32>(slot);
    }

    return static_cast<u32>(gatheredCount);
}

inline SceneShadingGpuData ResolveSceneShadingState(Core::ECS::World& world, const f32 fallbackAspectRatio, const u32 lightCount){
    SceneShadingGpuData state;
    const NWB::Impl::Scene::SceneViewBasis defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();

    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    const SIMDVector cameraPosition = cameraView.valid()
        ? LoadFloat(cameraView.transform->position)
        : LoadFloat(defaultBasis.positionDepthBias)
    ;
    StoreFloat(VectorSetW(cameraPosition, static_cast<f32>(lightCount)), &state.cameraPositionLightCount);
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

