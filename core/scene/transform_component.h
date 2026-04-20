// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(AlignedFloat4Data) TransformComponent{
    AlignedFloat3Data position = AlignedFloat3Data(0.f, 0.f, 0.f);
    // Unit quaternion in x/y/z/w order.
    AlignedFloat4Data rotation = AlignedFloat4Data(0.f, 0.f, 0.f, 1.f);
    AlignedFloat3Data scale = AlignedFloat3Data(1.f, 1.f, 1.f);
};

static_assert(IsStandardLayout_V<TransformComponent>, "TransformComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<TransformComponent>, "TransformComponent must stay cheap to move in dense ECS storage");
static_assert(
    alignof(TransformComponent) >= alignof(AlignedFloat4Data),
    "TransformComponent must stay aligned for SIMD component loads"
);
static_assert(
    sizeof(TransformComponent) == sizeof(AlignedFloat3Data) + sizeof(AlignedFloat4Data) + sizeof(AlignedFloat3Data),
    "TransformComponent must only contain aligned decomposed transform state"
);
static_assert(
    (sizeof(TransformComponent) % alignof(TransformComponent)) == 0,
    "TransformComponent array stride must keep every element SIMD-aligned"
);
static_assert(
    (offsetof(TransformComponent, position) % alignof(AlignedFloat3Data)) == 0,
    "TransformComponent::position must stay aligned"
);
static_assert(
    (offsetof(TransformComponent, rotation) % alignof(AlignedFloat4Data)) == 0,
    "TransformComponent::rotation must stay aligned"
);
static_assert(
    (offsetof(TransformComponent, scale) % alignof(AlignedFloat3Data)) == 0,
    "TransformComponent::scale must stay aligned"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

