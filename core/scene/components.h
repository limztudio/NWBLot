// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/ecs/entity_id.h>

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_BEGIN


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


struct SceneComponent{
    Core::ECS::EntityID mainCamera = Core::ECS::ENTITY_ID_INVALID;
};

static_assert(IsStandardLayout_V<SceneComponent>, "SceneComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<SceneComponent>, "SceneComponent must stay cheap to move in dense ECS storage");
static_assert(sizeof(SceneComponent) == sizeof(Core::ECS::EntityID), "SceneComponent must only contain shared scene entity references");


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

struct alignas(Float4) CameraProjectionData{
    Float4 projectionParams = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    f32 aspectRatio = 1.0f;
    f32 tanHalfVerticalFov = 0.0f;
};

static_assert(IsStandardLayout_V<CameraComponent>, "CameraComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<CameraComponent>, "CameraComponent must stay cheap to move in dense ECS storage");
static_assert(alignof(CameraComponent) >= alignof(Float4), "CameraComponent must stay aligned for SIMD component loads");
static_assert(sizeof(CameraComponent) == sizeof(Float4), "CameraComponent must stay one aligned vector wide");
static_assert((sizeof(CameraComponent) % alignof(CameraComponent)) == 0, "CameraComponent array stride must keep every element SIMD-aligned");
static_assert((offsetof(CameraComponent, projection) % alignof(Float4)) == 0, "CameraComponent::projection must stay aligned");
static_assert(IsStandardLayout_V<CameraProjectionData>, "CameraProjectionData must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CameraProjectionData>, "CameraProjectionData must stay cheap to pass by value");
static_assert(alignof(CameraProjectionData) >= alignof(Float4), "CameraProjectionData must keep projection params aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool TryComputeCameraTanHalfVerticalFov(const f32 verticalFovRadians, f32& outTanHalfFov){
    constexpr f32 s_CameraFovCosEpsilon = 0.000001f;

    outTanHalfFov = 0.0f;
    if(
        !IsFinite(verticalFovRadians)
        || verticalFovRadians <= 0.0f
        || verticalFovRadians >= s_PI
    )
        return false;

    SIMDVector sinHalfFovVector;
    SIMDVector cosHalfFovVector;
    VectorSinCos(&sinHalfFovVector, &cosHalfFovVector, VectorReplicate(verticalFovRadians * 0.5f));
    const f32 sinHalfFov = VectorGetX(sinHalfFovVector);
    const f32 cosHalfFov = VectorGetX(cosHalfFovVector);
    if(
        !IsFinite(sinHalfFov)
        || !IsFinite(cosHalfFov)
        || (cosHalfFov > -s_CameraFovCosEpsilon && cosHalfFov < s_CameraFovCosEpsilon)
    )
        return false;

    outTanHalfFov = sinHalfFov / cosHalfFov;
    return IsFinite(outTanHalfFov) && outTanHalfFov > 0.0f;
}

[[nodiscard]] inline bool CameraClipRangeValid(const CameraComponent& camera){
    return
        IsFinite(camera.nearPlane())
        && IsFinite(camera.farPlane())
        && camera.nearPlane() > 0.0f
        && camera.nearPlane() < camera.farPlane()
    ;
}

[[nodiscard]] inline f32 ResolveCameraAspectRatio(const CameraComponent& camera, const f32 fallbackAspectRatio){
    if(IsFinite(camera.aspectRatio()) && camera.aspectRatio() > 0.0f)
        return camera.aspectRatio();
    if(IsFinite(fallbackAspectRatio) && fallbackAspectRatio > 0.0f)
        return fallbackAspectRatio;
    return 1.0f;
}

[[nodiscard]] inline bool CameraProjectionDataValid(const CameraProjectionData& projectionData){
    const SIMDVector projectionParams = LoadFloat(projectionData.projectionParams);
    return
        IsFinite(projectionData.aspectRatio)
        && IsFinite(projectionData.tanHalfVerticalFov)
        && !Vector4IsNaN(projectionParams)
        && !Vector4IsInfinite(projectionParams)
        && projectionData.aspectRatio > 0.0f
        && projectionData.tanHalfVerticalFov > 0.0f
        && Vector3Greater(projectionParams, VectorZero())
        && VectorGetW(projectionParams) < 0.0f
    ;
}

[[nodiscard]] inline bool TryBuildCameraProjectionData(
    const CameraComponent& camera,
    const f32 fallbackAspectRatio,
    CameraProjectionData& outProjectionData
){
    outProjectionData = CameraProjectionData{};

    f32 tanHalfFov = 0.0f;
    if(
        !TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
        || !CameraClipRangeValid(camera)
    )
        return false;

    const f32 aspectRatio = ResolveCameraAspectRatio(camera, fallbackAspectRatio);
    const f32 depthRange = camera.farPlane() - camera.nearPlane();
    CameraProjectionData projectionData;
    projectionData.aspectRatio = aspectRatio;
    projectionData.tanHalfVerticalFov = tanHalfFov;
    projectionData.projectionParams = Float4(
        1.0f / (tanHalfFov * aspectRatio),
        1.0f / tanHalfFov,
        camera.farPlane() / depthRange,
        -(camera.nearPlane() * camera.farPlane()) / depthRange
    );
    if(!CameraProjectionDataValid(projectionData))
        return false;

    outProjectionData = projectionData;
    return true;
}

[[nodiscard]] inline bool TryBuildCameraProjectionParams(
    const CameraComponent& camera,
    const f32 fallbackAspectRatio,
    Float4& outProjectionParams
){
    CameraProjectionData projectionData;
    if(!TryBuildCameraProjectionData(camera, fallbackAspectRatio, projectionData)){
        outProjectionParams = Float4(0.0f, 0.0f, 0.0f, 0.0f);
        return false;
    }

    outProjectionParams = projectionData.projectionParams;
    return true;
}

[[nodiscard]] inline Float4 BuildDefaultCameraProjectionParams(const f32 fallbackAspectRatio = 1.0f){
    Float4 projectionParams;
    if(TryBuildCameraProjectionParams(CameraComponent{}, fallbackAspectRatio, projectionParams))
        return projectionParams;

    return Float4(1.0f, 1.0f, 1.0f, 0.0f);
}


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
        Float4 result;
        StoreFloat(VectorSetW(LoadFloat(colorIntensity), 0.0f), &result);
        return result;
    }
    [[nodiscard]] f32 intensity()const{ return colorIntensity.w; }

    void setColor(const Float4& value){
        StoreFloat(VectorSetW(LoadFloat(value), colorIntensity.w), &colorIntensity);
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


NWB_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

