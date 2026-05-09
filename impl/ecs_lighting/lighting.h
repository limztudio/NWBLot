// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "light_component.h"
#include <impl/ecs_scene/scene.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) SceneDirectionalLight{
    Float4 direction = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    Float4 colorIntensity = Float4(1.0f, 1.0f, 1.0f, 1.0f);
};

static_assert(IsStandardLayout_V<SceneDirectionalLight>, "SceneDirectionalLight must stay layout-stable");
static_assert(IsTriviallyCopyable_V<SceneDirectionalLight>, "SceneDirectionalLight must stay cheap to pass by value");
static_assert(alignof(SceneDirectionalLight) >= alignof(Float4), "SceneDirectionalLight must keep vectors aligned");


[[nodiscard]] SceneDirectionalLight BuildDefaultSceneDirectionalLight(const SceneViewBasis& basis);
[[nodiscard]] bool TryBuildSceneDirectionalLight(const TransformComponent& transform, const LightComponent& light, SceneDirectionalLight& outLight);
[[nodiscard]] SceneDirectionalLight ResolveSceneDirectionalLight(Core::ECS::World& world, const SceneViewBasis& defaultBasis);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

