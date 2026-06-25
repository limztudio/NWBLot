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
    const SIMDVector colorIntensity = LoadFloat(light.colorIntensity);
    const SIMDVector params = LoadFloat(light.params);
    const SIMDVector luminance = Vector3Dot(colorIntensity, VectorSet(0.2126f, 0.7152f, 0.0722f, 0.0f));
    const SIMDVector intensity = VectorMax(VectorSplatW(colorIntensity), VectorZero());
    const SIMDVector radiantImportance = VectorMultiply(luminance, intensity);

    if(VectorGetY(params) < 0.5f)
        return VectorGetX(VectorMultiplyAdd(radiantImportance, VectorReplicate(4.0f), s_SIMDOne));

    const SIMDVector delta = VectorSubtract(LoadFloat(light.position), LoadFloat(cameraPosition));
    const SIMDVector distanceSquared = Vector3LengthSq(delta);
    const SIMDVector range = VectorMax(VectorSplatX(params), VectorZero());
    const SIMDVector rangeSquared = VectorMultiply(range, range);
    const SIMDVector hasDistance = VectorGreater(distanceSquared, VectorReplicate(1e-4f));
    const SIMDVector safeDistanceSquared = VectorSelect(s_SIMDOne, distanceSquared, hasDistance);
    const SIMDVector coverageSquared = VectorSelect(
        s_SIMDOne,
        VectorDivide(rangeSquared, safeDistanceSquared),
        hasDistance
    );
    return VectorGetX(VectorMultiply(radiantImportance, VectorMin(coverageSquared, s_SIMDOne)));
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

