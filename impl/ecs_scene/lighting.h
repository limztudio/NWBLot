// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "view.h"

#include <core/ecs/entity_id.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::ECS::EntityID CreateDirectionalLightEntity(
    Core::ECS::World& world,
    f32 pitchRadians,
    f32 yawRadians,
    f32 rollRadians,
    const Float4& color,
    f32 intensity
);
Core::ECS::EntityID CreatePointLightEntity(
    Core::ECS::World& world,
    const Float4& position,
    const Float4& color,
    f32 intensity,
    f32 range
);
Core::ECS::EntityID CreateSpotLightEntity(
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


struct alignas(Float4) SceneLight{
    // xyz = world position (point/spot); w = spot inner cone cosine.
    Float4 position = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    // Directional: normalized direction toward the light. Spot: normalized emission axis. w = spot outer cone cosine.
    Float4 direction = Float4(0.0f, 0.0f, -1.0f, 1.0f);
    // xyz = color, w = intensity.
    Float4 colorIntensity = Float4(
        LightDefaults::s_WhiteColorComponent,
        LightDefaults::s_WhiteColorComponent,
        LightDefaults::s_WhiteColorComponent,
        LightDefaults::s_Intensity
    );
    f32 range = 0.0f;
    // Soft-shadow source size (see LightComponent): directional angular radius (radians) / punctual source radius (world units).
    f32 angularRadius = LightDefaults::s_DirectionalAngularRadius;
    f32 sourceRadius = LightDefaults::s_PunctualSourceRadius;
    // Byte-sized members kept last so the f32 fields pack contiguously with no internal padding.
    LightType::Enum type = LightType::Directional;
    bool enableCaustics = LightDefaults::s_EnableCaustics;
};

static_assert(IsStandardLayout_V<SceneLight>, "SceneLight must stay layout-stable");
static_assert(IsTriviallyCopyable_V<SceneLight>, "SceneLight must stay cheap to copy into GPU light lists");
static_assert(alignof(SceneLight) >= alignof(Float4), "SceneLight must keep vectors aligned");


// Fallback used when a world declares no lights: a single neutral directional light aimed along the view.
[[nodiscard]] SceneLight BuildDefaultSceneLight(const SceneViewBasis& basis);
[[nodiscard]] bool TryBuildSceneLight(const TransformComponent& transform, const LightComponent& light, SceneLight& outLight);
[[nodiscard]] usize GatherSceneLights(Core::ECS::World& world, const SceneViewBasis& defaultBasis, SceneLight* outLights, usize maxLights);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

