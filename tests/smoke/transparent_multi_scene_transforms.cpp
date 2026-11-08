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

[[nodiscard]] static SIMDVectorSingle BuildTransparentSceneRotation(const SIMDVectorSingle time)noexcept{
    return QuaternionRotationRollPitchYawFromVector(VectorAndInt(time, s_SIMDMaskY));
}

static void ApplyTransparentSceneTransform(const NWB::Core::ECS::EntityID entity, const Float4& basePosition, const SIMDVectorSingle sceneRotation, const SIMDVectorSingle localRotation, NWB::Core::ECS::World& world)noexcept{
    auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
    if(!transform)
        return;

    StoreFloat(Vector3Rotate(LoadFloat(basePosition), sceneRotation), transform->position);
    StoreFloat(QuaternionNormalize(QuaternionMultiply(sceneRotation, localRotation)), transform->rotation);
}

#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
[[nodiscard]] static SIMDVectorSingle BuildTransparentCsgRotation(const SIMDVectorSingle time)noexcept{
    const SIMDVectorSingle angles = VectorMultiply(time, VectorSet(0.32f, 1.0f, 0.16f, 0.0f));
    return QuaternionRotationRollPitchYawFromVector(angles);
}

static void ApplyTransparentCsgSceneTransform(
    const NWB::Core::ECS::EntityID receiverEntity,
    const NWB::Core::ECS::EntityID cutterEntity,
    const Float4& receiverBasePosition,
    const SIMDVectorSingle sceneRotation,
    const SIMDVectorSingle localRotation,
    NWB::Core::ECS::World& world
)noexcept{
    const SIMDVectorSingle receiverPosition = Vector3Rotate(LoadFloat(receiverBasePosition), sceneRotation);
    const SIMDVectorSingle receiverRotation = QuaternionNormalize(QuaternionMultiply(sceneRotation, localRotation));

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
    const SIMDVectorSingle sceneYaw = VectorReplicate(yaw);
    const SIMDVectorSingle sceneRotation = BuildTransparentSceneRotation(sceneYaw);
    ApplyTransparentSceneTransform(leftShape, s_TransparentLeftShapeBasePosition, sceneRotation, QuaternionIdentity(), world);
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    ApplyTransparentCsgSceneTransform(csgReceiver, csgCutter, s_TransparentCenterShapeBasePosition, sceneRotation, BuildTransparentCsgRotation(sceneYaw), world);
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

