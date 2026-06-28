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


inline constexpr f32 s_CausticSlotUnassigned = -1.f;
inline constexpr f32 s_CausticSlotDisabled = -2.f;


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
        dst.params = Float4(
            src.range,
            static_cast<f32>(src.type),
            -1.f,
            src.enableCaustics ? s_CausticSlotUnassigned : s_CausticSlotDisabled
        ); // z = shadow slot; w = caustic slot, or disabled when the light did not opt in
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

// True when the light is a caustic-eligible emitter: directional (params.y ~ 0) or spot (params.y ~ 2). Point
// lights (params.y ~ 1) are EXCLUDED in v1 -- omnidirectional emission would demand far too many photons.
// params.y carries static_cast<f32>(LightType::Enum) (Directional=0, Point=1, Spot=2; see ResolveSceneLights).
inline bool CausticLightEligible(const SceneLightGpuData& light){
    return light.params.y < 0.5f || light.params.y > 1.5f;
}

inline bool CausticLightEnabled(const SceneLightGpuData& light){
    return light.params.w >= (s_CausticSlotUnassigned - 0.5f);
}

// Caustic importance: pure radiant power (luminance * intensity), the same energy proxy ShadowSlotImportance
// uses, but WITHOUT the screen-coverage weighting (the caustic budget is aimed at the refractive occluders, not
// the camera). Higher = more worth a scarce caustic slot.
inline f32 CausticSlotImportance(const SceneLightGpuData& light){
    const f32 luminance = 0.2126f * light.colorIntensity.x + 0.7152f * light.colorIntensity.y + 0.0722f * light.colorIntensity.z;
    const f32 intensity = light.colorIntensity.w > 0.f ? light.colorIntensity.w : 0.f;
    return luminance * intensity;
}

// Assigns the bounded pool of NWB_SCENE_CAUSTIC_SLOT_COUNT caustic slots to the most important caustic-enabled,
// caustic-eligible lights, writing the chosen slot index into params.w (negative = no slot). Operates on the
// already-resolved light array (call AFTER ResolveSceneLights). CRUCIAL GATE: caustics only exist when the scene
// holds at least one refractive instance AND the light explicitly opted in; a normal transparent-shadow scene with
// refractive materials therefore remains a shadow test, not a photon-caustic test.
inline u32 ResolveCausticLights(SceneLightGpuData* outLights, const u32 lightCount, const u32 refractiveInstanceCount){
    if(refractiveInstanceCount == 0u || lightCount == 0u)
        return 0u;

    // Importance-ranked caustic-slot allocator (mirrors the shadow-slot K-pass selection in ResolveSceneLights):
    // hand each slot to the highest-importance eligible light without one yet. Point lights never qualify.
    u32 assignedCount = 0u;
    for(u32 slot = 0u; slot < NWB_SCENE_CAUSTIC_SLOT_COUNT; ++slot){
        f32 bestImportance = -1.f;
        u32 bestIndex = lightCount;
        for(u32 i = 0u; i < lightCount; ++i){
            if(outLights[i].params.w >= 0.f)
                continue;
            if(!CausticLightEnabled(outLights[i]))
                continue;
            if(!CausticLightEligible(outLights[i]))
                continue;
            const f32 importance = CausticSlotImportance(outLights[i]);
            if(importance > bestImportance){
                bestImportance = importance;
                bestIndex = i;
            }
        }
        if(bestIndex >= lightCount)
            break;
        outLights[bestIndex].params.w = static_cast<f32>(slot);
        ++assignedCount;
    }

    return assignedCount;
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

