// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

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
static_assert(
    alignof(TransformComponent) >= alignof(Float4),
    "TransformComponent must stay aligned for SIMD component loads"
);
static_assert(
    sizeof(TransformComponent) == sizeof(Float4) + sizeof(Float4) + sizeof(Float4),
    "TransformComponent must only contain aligned decomposed transform state"
);
static_assert(
    (sizeof(TransformComponent) % alignof(TransformComponent)) == 0,
    "TransformComponent array stride must keep every element SIMD-aligned"
);
static_assert(
    (offsetof(TransformComponent, position) % alignof(Float4)) == 0,
    "TransformComponent::position must stay aligned"
);
static_assert(
    (offsetof(TransformComponent, rotation) % alignof(Float4)) == 0,
    "TransformComponent::rotation must stay aligned"
);
static_assert(
    (offsetof(TransformComponent, scale) % alignof(Float4)) == 0,
    "TransformComponent::scale must stay aligned"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

