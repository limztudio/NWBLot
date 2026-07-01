// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "view.h"

#include <core/ecs/entity_id.h>

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LightType{
    enum Enum : u8{
        Directional,
        Point,
        Spot,

        kCount
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) LightComponent{
    // x/y/z = color, w = intensity.
    Float4 colorIntensity = Float4(1.0f, 1.0f, 1.0f, 1.0f);
    f32 range = 10.0f;
    // Spot cone cosines; outer (wider) must stay <= inner (narrower).
    f32 innerConeCos = 0.95f;
    f32 outerConeCos = 0.90f;
    // Soft-shadow source size (physical, Unreal-style). Directional: angular radius of the light disk in
    // radians (the sun half-angle; default ~0.27deg). Point/Spot: emissive sphere radius in world units.
    // Larger = softer penumbra. The RT sampler jitters the shadow ray over this source, so contact hardening
    // and distance-based softening emerge for free (no separate penumbra parameter).
    f32 angularRadius = 0.00465f;
    f32 sourceRadius = 0.1f;
    // Byte-sized members kept last so the five f32 above pack contiguously with no internal padding.
    LightType::Enum type = LightType::Directional;
    bool enableCaustics = true;

    [[nodiscard]] Float4 color()const{
        return Float4(colorIntensity.x, colorIntensity.y, colorIntensity.z, 0.0f);
    }
    [[nodiscard]] f32 intensity()const{ return colorIntensity.w; }

    void setColor(const Float4& value){
        colorIntensity.x = value.x;
        colorIntensity.y = value.y;
        colorIntensity.z = value.z;
    }
    void setIntensity(const f32 value){ colorIntensity.w = value; }
};

static_assert(sizeof(LightType::Enum) == sizeof(u8), "LightType must stay compact for ECS storage");
static_assert(IsStandardLayout_V<LightComponent>, "LightComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<LightComponent>, "LightComponent must stay cheap to move in dense ECS storage");
static_assert(alignof(LightComponent) >= alignof(Float4), "LightComponent must stay aligned for SIMD component loads");
static_assert(sizeof(LightComponent) == (sizeof(Float4) * 3), "LightComponent must stay three aligned vectors wide");
static_assert((sizeof(LightComponent) % alignof(LightComponent)) == 0, "LightComponent array stride must keep every element SIMD-aligned");
static_assert((offsetof(LightComponent, colorIntensity) % alignof(Float4)) == 0, "LightComponent::colorIntensity must stay aligned");
static_assert((offsetof(LightComponent, range) % alignof(f32)) == 0, "LightComponent::range must stay aligned");
static_assert((offsetof(LightComponent, innerConeCos) % alignof(f32)) == 0, "LightComponent::innerConeCos must stay aligned");
static_assert((offsetof(LightComponent, outerConeCos) % alignof(f32)) == 0, "LightComponent::outerConeCos must stay aligned");
static_assert((offsetof(LightComponent, type) % alignof(LightType::Enum)) == 0, "LightComponent::type must stay aligned");
static_assert((offsetof(LightComponent, enableCaustics) % alignof(bool)) == 0, "LightComponent::enableCaustics must stay aligned");
static_assert((offsetof(LightComponent, angularRadius) % alignof(f32)) == 0, "LightComponent::angularRadius must stay aligned");
static_assert((offsetof(LightComponent, sourceRadius) % alignof(f32)) == 0, "LightComponent::sourceRadius must stay aligned");


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
    Float4 colorIntensity = Float4(1.0f, 1.0f, 1.0f, 1.0f);
    f32 range = 0.0f;
    // Soft-shadow source size (see LightComponent): directional angular radius (radians) / punctual source radius (world units).
    f32 angularRadius = 0.00465f;
    f32 sourceRadius = 0.1f;
    // Byte-sized members kept last so the f32 fields pack contiguously with no internal padding.
    LightType::Enum type = LightType::Directional;
    bool enableCaustics = true;
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

