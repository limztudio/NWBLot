// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "payload_validation.h"
#include "skinned_types.h"

#include <core/assets/module.h>
#include <impl/assets/graphics/skinned_mesh/constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinnedMeshValidation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_Epsilon = static_cast<f32>(NWB_SKINNED_MESH_EPSILON);
static constexpr f32 s_SkinWeightSumEpsilon = 0.001f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MeshPayloadValidation::FiniteVector;

[[nodiscard]] inline bool ValidSkinInfluenceWeights(const SIMDVector weights){
    const f32 weightSum = VectorGetX(Vector4Dot(weights, s_SIMDOne));
    if(!FiniteVector(weights, 0xFu) || !Vector4GreaterOrEqual(weights, VectorZero()))
        return false;

    return Abs(weightSum - 1.0f) <= s_SkinWeightSumEpsilon;
}

[[nodiscard]] inline bool SkinInfluenceFitsSkeleton(const SkinInfluence4& skin, const u32 skeletonJointCount, u32& outJoint){
    outJoint = 0u;
    if(skeletonJointCount == 0u)
        return true;

    for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
        const u32 joint = static_cast<u32>(skin.joint[influenceIndex]);
        if(joint < skeletonJointCount)
            continue;

        outJoint = joint;
        return false;
    }
    return true;
}

[[nodiscard]] inline bool ValidAffineJointMatrix(const SIMDMatrix matrix){
    const SIMDVector column0 = matrix.v[0];
    const SIMDVector column1 = matrix.v[1];
    const SIMDVector column2 = matrix.v[2];
    const SIMDVector column3 = matrix.v[3];
    const SIMDVector affineW = VectorSet(
        VectorGetW(column0),
        VectorGetW(column1),
        VectorGetW(column2),
        VectorGetW(column3)
    );
    if(
        !FiniteVector(column0, 0xFu)
        || !FiniteVector(column1, 0xFu)
        || !FiniteVector(column2, 0xFu)
        || !FiniteVector(column3, 0xFu)
        || !Vector4NearEqual(affineW, s_SIMDIdentityR3, VectorReplicate(s_Epsilon))
    )
        return false;

    const f32 determinant = VectorGetX(Vector3Dot(
        VectorSetW(column0, 0.0f),
        Vector3Cross(VectorSetW(column1, 0.0f), VectorSetW(column2, 0.0f))
    ));
    return IsFinite(determinant) && Abs(determinant) > s_Epsilon;
}

[[nodiscard]] inline bool ValidInverseBindMatrices(
    const Core::Assets::AssetVector<SkeletonJointMatrix>& inverseBindMatrices,
    const u32 skeletonJointCount){
    if(inverseBindMatrices.empty())
        return true;
    if(skeletonJointCount == 0u || inverseBindMatrices.size() != skeletonJointCount)
        return false;

    for(const SkeletonJointMatrix& matrix : inverseBindMatrices){
        if(!ValidAffineJointMatrix(LoadFloat(matrix)))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

