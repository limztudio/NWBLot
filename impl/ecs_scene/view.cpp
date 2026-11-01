// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "view.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDMatrix LoadEntityTransformMatrix(Core::ECS::World& world, const Core::ECS::EntityID entity)noexcept{
    const TransformComponent* transform = world.tryGetComponent<TransformComponent>(entity);
    return transform
        ? LoadTransformMatrix(*transform)
        : MatrixIdentity()
    ;
}

void StoreDecomposedTransform(const MatrixDecomposition& decomposition, TransformComponent& transform)noexcept{
    StoreFloat(VectorSetW(decomposition.translation, 0.0f), transform.position);
    StoreFloat(decomposition.rotation, transform.rotation);
    StoreFloat(VectorSetW(decomposition.scale, 0.0f), transform.scale);
}

SIMDMatrix BuildDefaultSceneViewBasis()noexcept{
    SIMDVectorSingle sinAngles;
    SIMDVectorSingle cosAngles;
    VectorSinCos(sinAngles, cosAngles, VectorSet(s_DefaultSceneViewYaw, s_DefaultSceneViewPitch, 0.0f, 0.0f));

    const SIMDVectorSingle right = VectorPermute<4, 3, 0, 3>(sinAngles, cosAngles);
    const SIMDVectorSingle upBase = VectorMultiply(VectorPermute<0, 5, 4, 3>(sinAngles, cosAngles), VectorSet(1.0f, 1.0f, -1.0f, 0.0f));
    const SIMDVectorSingle upScale = VectorPermute<1, 4, 1, 3>(sinAngles, s_SIMDOne);
    const SIMDVectorSingle forwardBase = VectorMultiply(VectorPermute<0, 1, 4, 3>(sinAngles, cosAngles), VectorSet(-1.0f, 1.0f, 1.0f, 0.0f));
    const SIMDVectorSingle forwardScale = VectorPermute<5, 6, 5, 3>(sinAngles, cosAngles);
    const SIMDVectorSingle up = VectorMultiply(upBase, upScale);
    const SIMDVectorSingle forward = VectorMultiply(forwardBase, forwardScale);
    return SIMDMatrix{ .v = { right, up, forward, VectorSet(0.0f, 0.0f, 0.0f, s_DefaultSceneViewDepthOffset) } };
}

SIMDMatrix BuildSceneViewBasis(const SIMDVectorSingle position, const SIMDVectorSingle rotation)noexcept{
    const SIMDVectorSingle right = Vector3Rotate(s_SIMDIdentityR0, rotation);
    const SIMDVectorSingle up = Vector3Rotate(s_SIMDIdentityR1, rotation);
    const SIMDVectorSingle forward = Vector3Rotate(s_SIMDIdentityR2, rotation);
    return SIMDMatrix{ .v = { right, up, forward, position } };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

