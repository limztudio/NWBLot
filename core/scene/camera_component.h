// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(AlignedFloat4Data) CameraComponent{
    // x = vertical FOV, y = near plane, z = far plane, w = aspect ratio.
    // An aspect ratio of 0 lets renderers derive aspect from the active framebuffer.
    AlignedFloat4Data projection = AlignedFloat4Data(SimpleMath::ConvertToRadians(60.0f), 0.001f, 10000.0f, 0.0f);

    [[nodiscard]] f32 verticalFovRadians()const{ return projection.x; }
    [[nodiscard]] f32 nearPlane()const{ return projection.y; }
    [[nodiscard]] f32 farPlane()const{ return projection.z; }
    [[nodiscard]] f32 aspectRatio()const{ return projection.w; }

    void setVerticalFovRadians(const f32 value){ projection.x = value; }
    void setNearPlane(const f32 value){ projection.y = value; }
    void setFarPlane(const f32 value){ projection.z = value; }
    void setAspectRatio(const f32 value){ projection.w = value; }
};

struct alignas(AlignedFloat4Data) CameraProjectionData{
    AlignedFloat4Data projectionParams = AlignedFloat4Data(0.0f, 0.0f, 0.0f, 0.0f);
    f32 aspectRatio = 1.0f;
    f32 tanHalfVerticalFov = 0.0f;
};

static_assert(IsStandardLayout_V<CameraComponent>, "CameraComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<CameraComponent>, "CameraComponent must stay cheap to move in dense ECS storage");
static_assert(
    alignof(CameraComponent) >= alignof(AlignedFloat4Data),
    "CameraComponent must stay aligned for SIMD component loads"
);
static_assert(
    sizeof(CameraComponent) == sizeof(AlignedFloat4Data),
    "CameraComponent must stay one aligned vector wide"
);
static_assert(
    (sizeof(CameraComponent) % alignof(CameraComponent)) == 0,
    "CameraComponent array stride must keep every element SIMD-aligned"
);
static_assert(
    (offsetof(CameraComponent, projection) % alignof(AlignedFloat4Data)) == 0,
    "CameraComponent::projection must stay aligned"
);
static_assert(IsStandardLayout_V<CameraProjectionData>, "CameraProjectionData must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CameraProjectionData>, "CameraProjectionData must stay cheap to pass by value");
static_assert(
    alignof(CameraProjectionData) >= alignof(AlignedFloat4Data),
    "CameraProjectionData must keep projection params aligned"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool TryComputeCameraTanHalfVerticalFov(const f32 verticalFovRadians, f32& outTanHalfFov){
    constexpr f32 s_CameraFovCosEpsilon = 0.000001f;

    outTanHalfFov = 0.0f;
    if(!IsFinite(verticalFovRadians)
        || verticalFovRadians <= 0.0f
        || verticalFovRadians >= SimpleMath::Pi
    )
        return false;

    const f32 halfFov = verticalFovRadians * 0.5f;
    const f32 sinHalfFov = Sin(halfFov);
    const f32 cosHalfFov = Cos(halfFov);
    if(!IsFinite(sinHalfFov)
        || !IsFinite(cosHalfFov)
        || (cosHalfFov > -s_CameraFovCosEpsilon && cosHalfFov < s_CameraFovCosEpsilon)
    )
        return false;

    outTanHalfFov = sinHalfFov / cosHalfFov;
    return IsFinite(outTanHalfFov) && outTanHalfFov > 0.0f;
}

[[nodiscard]] inline bool CameraClipRangeValid(const CameraComponent& camera){
    return IsFinite(camera.nearPlane())
        && IsFinite(camera.farPlane())
        && camera.nearPlane() > 0.0f
        && camera.nearPlane() < camera.farPlane();
}

[[nodiscard]] inline f32 ResolveCameraAspectRatio(const CameraComponent& camera, const f32 fallbackAspectRatio){
    if(IsFinite(camera.aspectRatio()) && camera.aspectRatio() > 0.0f)
        return camera.aspectRatio();
    if(IsFinite(fallbackAspectRatio) && fallbackAspectRatio > 0.0f)
        return fallbackAspectRatio;
    return 1.0f;
}

[[nodiscard]] inline bool CameraProjectionDataValid(const CameraProjectionData& projectionData){
    return IsFinite(projectionData.aspectRatio)
        && IsFinite(projectionData.tanHalfVerticalFov)
        && IsFinite(projectionData.projectionParams.x)
        && IsFinite(projectionData.projectionParams.y)
        && IsFinite(projectionData.projectionParams.z)
        && IsFinite(projectionData.projectionParams.w)
        && projectionData.aspectRatio > 0.0f
        && projectionData.tanHalfVerticalFov > 0.0f
        && projectionData.projectionParams.x > 0.0f
        && projectionData.projectionParams.y > 0.0f
        && projectionData.projectionParams.z > 0.0f
        && projectionData.projectionParams.w < 0.0f;
}

[[nodiscard]] inline bool TryBuildCameraProjectionData(
    const CameraComponent& camera,
    const f32 fallbackAspectRatio,
    CameraProjectionData& outProjectionData
){
    outProjectionData = CameraProjectionData{};

    f32 tanHalfFov = 0.0f;
    if(!TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
        || !CameraClipRangeValid(camera)
    )
        return false;

    const f32 aspectRatio = ResolveCameraAspectRatio(camera, fallbackAspectRatio);
    const f32 depthRange = camera.farPlane() - camera.nearPlane();
    CameraProjectionData projectionData;
    projectionData.aspectRatio = aspectRatio;
    projectionData.tanHalfVerticalFov = tanHalfFov;
    projectionData.projectionParams = AlignedFloat4Data(
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
    AlignedFloat4Data& outProjectionParams
){
    CameraProjectionData projectionData;
    if(!TryBuildCameraProjectionData(camera, fallbackAspectRatio, projectionData)){
        outProjectionParams = AlignedFloat4Data(0.0f, 0.0f, 0.0f, 0.0f);
        return false;
    }

    outProjectionParams = projectionData.projectionParams;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

