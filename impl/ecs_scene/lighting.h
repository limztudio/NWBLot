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

        kCount
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) LightComponent{
    // x/y/z = color, w = intensity.
    Float4 colorIntensity = Float4(1.0f, 1.0f, 1.0f, 1.0f);
    f32 range = 10.0f;
    LightType::Enum type = LightType::Directional;

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
static_assert(sizeof(LightComponent) == (sizeof(Float4) * 2), "LightComponent must stay two aligned vectors wide");
static_assert((sizeof(LightComponent) % alignof(LightComponent)) == 0, "LightComponent array stride must keep every element SIMD-aligned");
static_assert((offsetof(LightComponent, colorIntensity) % alignof(Float4)) == 0, "LightComponent::colorIntensity must stay aligned");
static_assert((offsetof(LightComponent, range) % alignof(f32)) == 0, "LightComponent::range must stay aligned");
static_assert((offsetof(LightComponent, type) % alignof(LightType::Enum)) == 0, "LightComponent::type must stay aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) SceneDirectionalLight{
    Float4 direction = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    Float4 colorIntensity = Float4(1.0f, 1.0f, 1.0f, 1.0f);
};

static_assert(IsStandardLayout_V<SceneDirectionalLight>, "SceneDirectionalLight must stay layout-stable");
static_assert(IsTriviallyCopyable_V<SceneDirectionalLight>, "SceneDirectionalLight must stay cheap to pass by value");
static_assert(alignof(SceneDirectionalLight) >= alignof(Float4), "SceneDirectionalLight must keep vectors aligned");


[[nodiscard]] SceneDirectionalLight BuildDefaultSceneDirectionalLight(const SceneViewBasis& basis);
Core::ECS::EntityID CreateDirectionalLightEntity(
    Core::ECS::World& world,
    f32 pitchRadians,
    f32 yawRadians,
    f32 rollRadians,
    const Float4& color,
    f32 intensity
);
[[nodiscard]] bool TryBuildSceneDirectionalLight(const TransformComponent& transform, const LightComponent& light, SceneDirectionalLight& outLight);
[[nodiscard]] SceneDirectionalLight ResolveSceneDirectionalLight(Core::ECS::World& world, const SceneViewBasis& defaultBasis);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) SceneLight{
    // xyz = world position (point/spot lights).
    Float4 position = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    // xyz = normalized direction (directional/spot lights).
    Float4 direction = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    // xyz = color, w = intensity.
    Float4 colorIntensity = Float4(1.0f, 1.0f, 1.0f, 1.0f);
    f32 range = 0.0f;
    f32 innerConeCos = 1.0f;
    f32 outerConeCos = 1.0f;
    LightType::Enum type = LightType::Directional;
};

static_assert(IsStandardLayout_V<SceneLight>, "SceneLight must stay layout-stable");
static_assert(IsTriviallyCopyable_V<SceneLight>, "SceneLight must stay cheap to copy into GPU light lists");
static_assert(alignof(SceneLight) >= alignof(Float4), "SceneLight must keep vectors aligned");


[[nodiscard]] bool TryBuildSceneLight(const TransformComponent& transform, const LightComponent& light, SceneLight& outLight);
[[nodiscard]] usize GatherSceneLights(Core::ECS::World& world, const SceneViewBasis& defaultBasis, SceneLight* outLights, usize maxLights);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

