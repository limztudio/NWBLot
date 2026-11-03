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
    return LoadFloat(BuildDefaultSceneViewBasisRows());
}

SIMDMatrix BuildSceneViewBasis(const SIMDVectorSingle position, const SIMDVectorSingle rotation)noexcept{
    Float4 posVec;
    Float4 rotVec;
    StoreFloat(position, posVec);
    StoreFloat(rotation, rotVec);
    return LoadFloat(BuildSceneViewBasisRows(posVec, rotVec));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

