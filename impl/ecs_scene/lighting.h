// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "view.h"

#include <core/ecs/entity_id.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Core::ECS::EntityID CreateDirectionalLightEntity(Core::ECS::World& world, f32 pitchRadians, f32 yawRadians, f32 rollRadians, const Float4& color, f32 intensity);
[[nodiscard]] Core::ECS::EntityID CreatePointLightEntity(Core::ECS::World& world, const Float4& position, const Float4& color, f32 intensity, f32 range);
[[nodiscard]] Core::ECS::EntityID CreateSpotLightEntity(Core::ECS::World& world, const Float4& position, f32 pitchRadians, f32 yawRadians, f32 rollRadians, const Float4& color, f32 intensity, f32 range, f32 innerConeCos, f32 outerConeCos);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) SceneLight{
    // xyz = world position (point/spot); w = spot inner cone cosine.
    Float4 position = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    // Directional: direction toward light. Spot: emission axis. w = outer cone cosine.
    Float4 direction = Float4(0.0f, 0.0f, -1.0f, 1.0f);
    // xyz = color, w = intensity.
    Float4 colorIntensity = Float4(s_LightWhiteColorComponent, s_LightWhiteColorComponent, s_LightWhiteColorComponent, s_LightIntensity);
    f32 range = 0.0f;
    // Soft-shadow source size: angular radius (radians) / punctual radius (world units).
    f32 angularRadius = s_LightDirectionalAngularRadius;
    f32 sourceRadius = s_LightPunctualSourceRadius;
    LightType::Enum type = LightType::Directional;
    b8 enableCaustics = s_LightEnableCaustics;
};

static_assert(sizeof(LightType::Enum) == sizeof(u8), "LightType must stay compact for ECS storage");
static_assert(IsStandardLayout_V<LightComponent>, "LightComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<LightComponent>, "LightComponent must stay cheap to move in dense ECS storage");
static_assert(alignof(LightComponent) >= alignof(Float4), "LightComponent must stay aligned for SIMD component loads");
static_assert(sizeof(LightComponent) == (sizeof(Float4) * 3), "LightComponent must stay three aligned vectors wide");
static_assert((sizeof(LightComponent) % alignof(LightComponent)) == 0u, "LightComponent array stride must keep every element SIMD-aligned");
static_assert((offsetof(LightComponent, colorIntensity) % alignof(Float4)) == 0u, "LightComponent::colorIntensity must stay aligned");
static_assert((offsetof(LightComponent, range) % alignof(f32)) == 0u, "LightComponent::range must stay aligned");
static_assert((offsetof(LightComponent, innerConeCos) % alignof(f32)) == 0u, "LightComponent::innerConeCos must stay aligned");
static_assert((offsetof(LightComponent, outerConeCos) % alignof(f32)) == 0u, "LightComponent::outerConeCos must stay aligned");
static_assert((offsetof(LightComponent, type) % alignof(LightType::Enum)) == 0u, "LightComponent::type must stay aligned");
static_assert((offsetof(LightComponent, enableCaustics) % alignof(b8)) == 0u, "LightComponent::enableCaustics must stay aligned");
static_assert((offsetof(LightComponent, angularRadius) % alignof(f32)) == 0u, "LightComponent::angularRadius must stay aligned");
static_assert((offsetof(LightComponent, sourceRadius) % alignof(f32)) == 0u, "LightComponent::sourceRadius must stay aligned");

static_assert(IsStandardLayout_V<SceneLight>, "SceneLight must stay layout-stable");
static_assert(IsTriviallyCopyable_V<SceneLight>, "SceneLight must stay cheap to pass through scene preparation");
static_assert(alignof(SceneLight) >= alignof(Float4), "SceneLight must keep storage vectors aligned");


[[nodiscard]] usize GatherSceneLights(Core::ECS::World& world, SIMDVectorSingle defaultForward, usize maxLights, SceneLight* outLights)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

