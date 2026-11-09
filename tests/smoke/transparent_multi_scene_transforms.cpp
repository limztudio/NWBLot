// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "transparent_multi_scene_transforms.h"

#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
#include "csg_smoke_helpers.h"
#include <impl/ecs_csg/module.h>
#endif
#include <impl/ecs_scene/module.h>
#include <core/ecs/world.h>
#include <global/simd/frame.h>
#include <global/simd/constant.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_transparent_multi_scene_transforms{

#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
using NWB::Tests::Smoke::AssignCsgCutterTransform;
#endif

static void ApplyTransparentSceneTransform(const NWB::Core::ECS::EntityID entity, const Float4& basePosition, const SIMDVectorSingle sceneRotation, const SIMDVectorSingle localRotation, NWB::Core::ECS::World& world)noexcept{
    auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
    if(!transform)
        return;

    SIMDVectorSingle position;
    SIMDVectorSingle rotation;
    ComputeTransparentSceneTransform(LoadFloat(basePosition), sceneRotation, localRotation, position, rotation);
    StoreFloat(position, transform->position);
    StoreFloat(rotation, transform->rotation);
}

#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
static void ApplyTransparentCsgSceneTransform(
    const NWB::Core::ECS::EntityID receiverEntity,
    const NWB::Core::ECS::EntityID cutterEntity,
    const Float4& receiverBasePosition,
    const SIMDVectorSingle sceneRotation,
    const SIMDVectorSingle localRotation,
    NWB::Core::ECS::World& world
)noexcept{
    SIMDVectorSingle receiverPosition;
    SIMDVectorSingle receiverRotation;
    ComputeTransparentSceneTransform(LoadFloat(receiverBasePosition), sceneRotation, localRotation, receiverPosition, receiverRotation);

    if(auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(receiverEntity)){
        StoreFloat(receiverPosition, transform->position);
        StoreFloat(receiverRotation, transform->rotation);
    }

    if(auto* cutter = world.tryGetComponent<NWB::Impl::CsgCutterComponent>(cutterEntity))
        AssignCsgCutterTransform(receiverPosition, receiverRotation, *cutter);
}
#endif

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TransparentMultiSceneTransforms::update(const f32 yaw, Core::ECS::World& world)const noexcept{
    using namespace __hidden_transparent_multi_scene_transforms;
    const SIMDVectorSingle sceneRotation = BuildTransparentSceneRotation(yaw);
    ApplyTransparentSceneTransform(leftShape, s_TransparentLeftShapeBasePosition, sceneRotation, QuaternionIdentity(), world);
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    ApplyTransparentCsgSceneTransform(csgReceiver, csgCutter, s_TransparentCenterShapeBasePosition, sceneRotation, BuildTransparentCsgRotation(yaw), world);
#else
    ApplyTransparentSceneTransform(centerShape, s_TransparentCenterShapeBasePosition, sceneRotation, QuaternionIdentity(), world);
#endif
    ApplyTransparentSceneTransform(rightShape, s_TransparentRightShapeBasePosition, sceneRotation, QuaternionIdentity(), world);
    ApplyTransparentSceneTransform(opaqueLeftShape, s_OpaqueLeftShapeBasePosition, sceneRotation, QuaternionIdentity(), world);
    ApplyTransparentSceneTransform(opaqueRightShape, s_OpaqueRightShapeBasePosition, sceneRotation, QuaternionIdentity(), world);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
