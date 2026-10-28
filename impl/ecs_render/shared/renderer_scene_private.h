// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/assets/graphics/scene/binding_slots.h>
#include <impl/ecs_scene/module.h>

#include <core/ecs/world.h>

#include <global/simd/vector.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_ShadowSlotUnassigned = -1.f;
inline constexpr f32 s_CausticSlotUnassigned = s_ShadowSlotUnassigned;
inline constexpr f32 s_CausticSlotDisabled = -2.f;
inline constexpr f32 s_CausticSlotEnabledThreshold = s_CausticSlotUnassigned - 0.5f;
inline constexpr f32 s_Rec709LuminanceRed = 0.2126f;
inline constexpr f32 s_Rec709LuminanceGreen = 0.7152f;
inline constexpr f32 s_Rec709LuminanceBlue = 0.0722f;
inline constexpr f32 s_DirectionalShadowImportanceBoost = 4.0f;
inline constexpr f32 s_ShadowImportanceDistanceSquaredEpsilon = 1e-4f;

// SceneLightGpuData::params.y carries static_cast<f32>(LightType::Enum) (Directional=0, Point=1, Spot=2).
// params.y packs the light type as float. Half-way bounds absorb float round-trip noise.
inline constexpr f32 s_LightTypeDirectionalMax = static_cast<f32>(NWB_SCENE_LIGHT_TYPE_DIRECTIONAL_MAX);
inline constexpr f32 s_LightTypePointMax = static_cast<f32>(NWB_SCENE_LIGHT_TYPE_POINT_MAX);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Importance heuristic for the bounded shadow-slot pool: radiant power (luminance * intensity) weighted by screen coverage.
// Directionals cover the frame and outrank locals; locals weight by range/distance squared (no sqrt).
// Higher = more worth a scarce shadow slot.
inline SIMDVectorSingle ShadowSlotImportance(const SIMDVectorSingle colorIntensity, const SIMDVectorSingle params, const SIMDVectorSingle lightPosition, const SIMDVectorSingle cameraPosition)noexcept{
    const SIMDVectorSingle luminance = Vector3Dot(colorIntensity, VectorSet(s_Rec709LuminanceRed, s_Rec709LuminanceGreen, s_Rec709LuminanceBlue, 0.0f));
    const SIMDVectorSingle intensity = VectorMax(VectorSplatW(colorIntensity), VectorZero());
    const SIMDVectorSingle radiantImportance = VectorMultiply(luminance, intensity);

    if(Vector4Less(VectorSplatY(params), VectorReplicate(s_LightTypeDirectionalMax)))
        return VectorMultiplyAdd(radiantImportance, VectorReplicate(s_DirectionalShadowImportanceBoost), s_SIMDOne);

    const SIMDVectorSingle delta = VectorSubtract(lightPosition, cameraPosition);
    const SIMDVectorSingle distanceSquared = Vector3LengthSq(delta);
    const SIMDVectorSingle range = VectorMax(VectorSplatX(params), VectorZero());
    const SIMDVectorSingle rangeSquared = VectorMultiply(range, range);
    const SIMDVectorSingle hasDistance = VectorGreater(distanceSquared, VectorReplicate(s_ShadowImportanceDistanceSquaredEpsilon));
    const SIMDVectorSingle safeDistanceSquared = VectorSelect(s_SIMDOne, distanceSquared, hasDistance);
    const SIMDVectorSingle coverageSquared = VectorSelect(s_SIMDOne, VectorDivide(rangeSquared, safeDistanceSquared), hasDistance);
    return VectorMultiply(radiantImportance, VectorMin(coverageSquared, s_SIMDOne));
}

// Caustic importance: pure radiant power, no screen weighting (budget targets occluders, not camera).
// Higher = more worth a scarce caustic slot.
inline SIMDVectorSingle CausticSlotImportance(const SIMDVectorSingle colorIntensity)noexcept{
    const SIMDVectorSingle luminance = Vector3Dot(colorIntensity, VectorSet(s_Rec709LuminanceRed, s_Rec709LuminanceGreen, s_Rec709LuminanceBlue, 0.0f));
    const SIMDVectorSingle intensity = VectorMax(VectorSplatW(colorIntensity), VectorZero());
    return VectorMultiply(luminance, intensity);
}

inline u32 ResolveSceneLights(Core::ECS::World& world, const u32 maxLights, SceneLightGpuData* outLights, f32* outCausticImportance)noexcept{
    if(maxLights == 0u)
        return 0u;

    u32 capacity = maxLights;
    if(capacity > NWB_SCENE_MAX_LIGHTS)
        capacity = NWB_SCENE_MAX_LIGHTS;

    const SIMDMatrix defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();
    NWB::Impl::Scene::SceneLight sceneLights[NWB_SCENE_MAX_LIGHTS];
    f32 shadowImportance[NWB_SCENE_MAX_LIGHTS] = {};
    const usize gatheredCount = NWB::Impl::Scene::GatherSceneLights(world, defaultBasis.v[2], capacity, sceneLights);

    for(usize i = 0u; i < gatheredCount; ++i){
        const NWB::Impl::Scene::SceneLight& src = sceneLights[i];
        SceneLightGpuData& dst = outLights[i];
        dst.position = src.position;
        dst.direction = src.direction;
        dst.colorIntensity = src.colorIntensity;
        dst.params = Float4(src.range, static_cast<f32>(src.type), s_ShadowSlotUnassigned, src.enableCaustics ? s_CausticSlotUnassigned : s_CausticSlotDisabled); // z = shadow slot; w = caustic slot, or disabled when the light did not opt in
        dst.params2 = Float4(src.angularRadius, src.sourceRadius, 0.f, 0.f); // soft-shadow source size
        outCausticImportance[i] = VectorGetX(CausticSlotImportance(LoadFloat(src.colorIntensity)));
    }

    // Importance-ranked shadow slots: most important lights get the pool (slot -> params.z; rest stay lit).
    // Selection over <= NWB_SCENE_MAX_LIGHTS lights stays cheap.
    const u32 lightCount = static_cast<u32>(gatheredCount);
    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, NWB::Impl::Scene::s_CameraFallbackAspectRatio);
    const SIMDVectorSingle cameraPosition = cameraView.valid() ? LoadFloat(cameraView.transform->position) : VectorZero();
    for(u32 i = 0u; i < lightCount; ++i){
        const NWB::Impl::Scene::SceneLight& light = sceneLights[i];
        shadowImportance[i] = VectorGetX(ShadowSlotImportance(
            LoadFloat(light.colorIntensity),
            VectorSet(light.range, static_cast<f32>(light.type), s_ShadowSlotUnassigned, light.enableCaustics ? s_CausticSlotUnassigned : s_CausticSlotDisabled),
            LoadFloat(light.position),
            cameraPosition
        ));
    }

    const u32 slotCount = (lightCount < NWB_SCENE_SHADOW_SLOT_COUNT) ? lightCount : NWB_SCENE_SHADOW_SLOT_COUNT;
    for(u32 slot = 0u; slot < slotCount; ++slot){
        f32 bestImportance = -1.f;
        u32 bestIndex = lightCount;
        for(u32 i = 0u; i < lightCount; ++i){
            if(outLights[i].params.z >= 0.f)
                continue;
            const f32 importance = shadowImportance[i];
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

// True when the light is a caustic-eligible emitter: directional (params.y ~ 0) or spot (params.y ~ 2).
// Point lights (params.y ~ 1) are excluded because omnidirectional emission would spread the photon budget too thin.
// params.y carries static_cast<f32>(LightType::Enum) (Directional=0, Point=1, Spot=2; see ResolveSceneLights).
inline bool CausticLightEligible(const SceneLightGpuData& light)noexcept{
    return light.params.y < s_LightTypeDirectionalMax || light.params.y > s_LightTypePointMax;
}

inline bool CausticLightEnabled(const SceneLightGpuData& light)noexcept{
    return light.params.w >= s_CausticSlotEnabledThreshold;
}

// Assigns caustic slots to top caustic-enabled lights after ResolveSceneLights when a refractive instance and caustic opt-in are present.
inline u32 ResolveCausticLights(const Span<const f32> causticImportance, const u32 refractiveInstanceCount, const Span<SceneLightGpuData> lights)noexcept{
    const u32 lightCount = static_cast<u32>(lights.size());
    if(refractiveInstanceCount == 0u || lightCount == 0u || causticImportance.size() < lightCount)
        return 0u;

    // Importance-ranked caustic slots (mirrors shadow selection). Highest eligible light per slot; points never qualify.
    u32 assignedCount = 0u;
    for(u32 slot = 0u; slot < NWB_SCENE_CAUSTIC_SLOT_COUNT; ++slot){
        f32 bestImportance = -1.f;
        u32 bestIndex = lightCount;
        for(u32 i = 0u; i < lightCount; ++i){
            if(lights[i].params.w >= 0.f)
                continue;
            if(!CausticLightEnabled(lights[i]))
                continue;
            if(!CausticLightEligible(lights[i]))
                continue;
            const f32 importance = causticImportance[i];
            if(importance > bestImportance){
                bestImportance = importance;
                bestIndex = i;
            }
        }
        if(bestIndex >= lightCount)
            break;
        lights[bestIndex].params.w = static_cast<f32>(slot);
        ++assignedCount;
    }

    return assignedCount;
}

inline SceneShadingGpuData ResolveSceneShadingState(Core::ECS::World& world, const f32 fallbackAspectRatio, const u32 lightCount)noexcept{
    SceneShadingGpuData state;
    const SIMDMatrix defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();

    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    const SIMDVectorSingle cameraPosition = cameraView.valid() ? LoadFloat(cameraView.transform->position) : defaultBasis.v[3];
    StoreFloat(VectorSetW(cameraPosition, static_cast<f32>(lightCount)), state.cameraPositionLightCount);
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

