// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "view.h"

#include <core/ecs/entity_id.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Core::ECS::EntityID CreateDirectionalLightEntity(
    Core::ECS::World& world,
    f32 pitchRadians,
    f32 yawRadians,
    f32 rollRadians,
    const Float4& color,
    f32 intensity
);
[[nodiscard]] Core::ECS::EntityID CreatePointLightEntity(
    Core::ECS::World& world,
    const Float4& position,
    const Float4& color,
    f32 intensity,
    f32 range
);
[[nodiscard]] Core::ECS::EntityID CreateSpotLightEntity(
    Core::ECS::World& world,
    const Float4& position,
    f32 pitchRadians,
    f32 yawRadians,
    f32 rollRadians,
    const Float4& color,
    f32 intensity,
    f32 range,
    f32 innerConeCos,
    f32 outerConeCos
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(SIMDVector) SceneLight{
    // xyz = world position (point/spot); w = spot inner cone cosine.
    SIMDVector position = s_SIMDIdentityR3;
    // Directional: normalized direction toward the light. Spot: normalized emission axis. w = spot outer cone cosine.
    SIMDVector direction = VectorSet(0.0f, 0.0f, -1.0f, 1.0f);
    // xyz = color, w = intensity.
    SIMDVector colorIntensity = s_SIMDOne;
    f32 range = 0.0f;
    // Soft-shadow source size (see LightComponent): directional angular radius (radians) / punctual source radius (world units).
    f32 angularRadius = LightDefaults::s_DirectionalAngularRadius;
    f32 sourceRadius = LightDefaults::s_PunctualSourceRadius;
    // Byte-sized members kept last so the f32 fields pack contiguously with no internal padding.
    LightType::Enum type = LightType::Directional;
    bool enableCaustics = LightDefaults::s_EnableCaustics;
};

static_assert(IsStandardLayout_V<SceneLight>, "SceneLight must stay layout-stable");
static_assert(IsTriviallyCopyable_V<SceneLight>, "SceneLight must stay cheap to pass through scene preparation");
static_assert(alignof(SceneLight) >= alignof(SIMDVector), "SceneLight must keep calculation vectors aligned");


// Fallback used when a world declares no lights: a single neutral directional light aimed along the view.
[[nodiscard]] SceneLight BuildDefaultSceneLight(SIMDVector forward);
[[nodiscard]] bool TryBuildSceneLight(
    SIMDVector position,
    SIMDVector rotation,
    SIMDVector colorIntensity,
    f32 range,
    f32 innerConeCos,
    f32 outerConeCos,
    f32 angularRadius,
    f32 sourceRadius,
    LightType::Enum type,
    bool enableCaustics,
    SceneLight& outLight
);
[[nodiscard]] usize GatherSceneLights(Core::ECS::World& world, SIMDVector defaultForward, SceneLight* outLights, usize maxLights);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

