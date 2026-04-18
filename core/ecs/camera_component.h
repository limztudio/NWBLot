// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/matrix_math.h>

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(AlignedFloat4Data) CameraComponent{
    f32 verticalFovRadians = SourceMath::ConvertToRadians(60.0f);
    f32 nearPlane = 0.1f;
    f32 farPlane = 100.0f;
    f32 aspectRatio = 1.0f;
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
    (offsetof(CameraComponent, verticalFovRadians) % alignof(f32)) == 0,
    "CameraComponent::verticalFovRadians must stay aligned"
);
static_assert((offsetof(CameraComponent, nearPlane) % alignof(f32)) == 0, "CameraComponent::nearPlane must stay aligned");
static_assert((offsetof(CameraComponent, farPlane) % alignof(f32)) == 0, "CameraComponent::farPlane must stay aligned");
static_assert((offsetof(CameraComponent, aspectRatio) % alignof(f32)) == 0, "CameraComponent::aspectRatio must stay aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

