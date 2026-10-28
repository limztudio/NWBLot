// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_scene/components.adhd.h>

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(IsStandardLayout_V<TransformComponent>, "TransformComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<TransformComponent>, "TransformComponent must stay cheap to move in dense ECS storage");
static_assert(alignof(TransformComponent) >= alignof(Float4), "TransformComponent must stay aligned for SIMD component loads");
static_assert(sizeof(TransformComponent) == sizeof(Float4) + sizeof(Float4) + sizeof(Float4), "TransformComponent must only contain aligned decomposed transform state");
static_assert((sizeof(TransformComponent) % alignof(TransformComponent)) == 0u, "TransformComponent array stride must keep every element SIMD-aligned");
static_assert((offsetof(TransformComponent, position) % alignof(Float4)) == 0u, "TransformComponent::position must stay aligned");
static_assert((offsetof(TransformComponent, rotation) % alignof(Float4)) == 0u, "TransformComponent::rotation must stay aligned");
static_assert((offsetof(TransformComponent, scale) % alignof(Float4)) == 0u, "TransformComponent::scale must stay aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Calculation rows are right, up, forward, and position/depth bias; never retained as scene storage.
[[nodiscard]] SIMDMatrix BuildDefaultSceneViewBasis()noexcept;
[[nodiscard]] SIMDMatrix BuildSceneViewBasis(SIMDVectorSingle position, SIMDVectorSingle rotation)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

