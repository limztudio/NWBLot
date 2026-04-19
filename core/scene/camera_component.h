// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/matrix_math.h>

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(AlignedFloat4Data) CameraComponent{
    // x = vertical FOV, y = near plane, z = far plane, w = aspect ratio.
    // An aspect ratio of 0 lets renderers derive aspect from the active framebuffer.
    AlignedFloat4Data projection = AlignedFloat4Data(SourceMath::ConvertToRadians(60.0f), 0.001f, 10000.0f, 0.0f);

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

