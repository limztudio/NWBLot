// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/core/ecs/entity_id.h>

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) TransformComponent{
    Float4 position = Float4(0.f, 0.f, 0.f);
    // Unit quaternion in x/y/z/w order.
    Float4 rotation = Float4(0.f, 0.f, 0.f, 1.f);
    Float4 scale = Float4(1.f, 1.f, 1.f);
};

static_assert(IsStandardLayout_V<TransformComponent>, "TransformComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<TransformComponent>, "TransformComponent must stay cheap to move in dense ECS storage");
static_assert(alignof(TransformComponent) >= alignof(Float4), "TransformComponent must stay aligned for SIMD component loads");
static_assert(sizeof(TransformComponent) == sizeof(Float4) + sizeof(Float4) + sizeof(Float4), "TransformComponent must only contain aligned decomposed transform state");
static_assert((sizeof(TransformComponent) % alignof(TransformComponent)) == 0, "TransformComponent array stride must keep every element SIMD-aligned");
static_assert((offsetof(TransformComponent, position) % alignof(Float4)) == 0, "TransformComponent::position must stay aligned");
static_assert((offsetof(TransformComponent, rotation) % alignof(Float4)) == 0, "TransformComponent::rotation must stay aligned");
static_assert((offsetof(TransformComponent, scale) % alignof(Float4)) == 0, "TransformComponent::scale must stay aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ActiveCameraComponent{
    Core::ECS::EntityID camera = Core::ECS::ENTITY_ID_INVALID;
};

static_assert(IsStandardLayout_V<ActiveCameraComponent>, "ActiveCameraComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<ActiveCameraComponent>, "ActiveCameraComponent must stay cheap to move in dense ECS storage");
static_assert(sizeof(ActiveCameraComponent) == sizeof(Core::ECS::EntityID), "ActiveCameraComponent must only contain the active camera entity reference");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) CameraComponent{
    // x = vertical FOV, y = near plane, z = far plane, w = aspect ratio.
    // An aspect ratio of 0 lets renderers derive aspect from the active framebuffer.
    Float4 projection = Float4(60.0f * (s_PI / 180.0f), 0.001f, 10000.0f, 0.0f);

    [[nodiscard]] f32 verticalFovRadians()const{ return projection.x; }
    [[nodiscard]] f32 nearPlane()const{ return projection.y; }
    [[nodiscard]] f32 farPlane()const{ return projection.z; }
    [[nodiscard]] f32 aspectRatio()const{ return projection.w; }

    void setVerticalFovRadians(const f32 value){ projection.x = value; }
    void setNearPlane(const f32 value){ projection.y = value; }
    void setFarPlane(const f32 value){ projection.z = value; }
    void setAspectRatio(const f32 value){ projection.w = value; }
};

static_assert(IsStandardLayout_V<CameraComponent>, "CameraComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<CameraComponent>, "CameraComponent must stay cheap to move in dense ECS storage");
static_assert(alignof(CameraComponent) >= alignof(Float4), "CameraComponent must stay aligned for SIMD component loads");
static_assert(sizeof(CameraComponent) == sizeof(Float4), "CameraComponent must stay one aligned vector wide");
static_assert((sizeof(CameraComponent) % alignof(CameraComponent)) == 0, "CameraComponent array stride must keep every element SIMD-aligned");
static_assert((offsetof(CameraComponent, projection) % alignof(Float4)) == 0, "CameraComponent::projection must stay aligned");


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


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

