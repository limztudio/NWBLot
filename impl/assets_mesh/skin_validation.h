#pragma once


#include "payload_validation.h"
#include "skin_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinValidation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_Epsilon = 0.000001f;
static constexpr f32 s_SkinWeightSumEpsilon = 0.001f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ValidSkinInfluenceWeights(const SIMDVector weights){
    if(!VectorIsFinite(weights, 0xFu) || !Vector4GreaterOrEqual(weights, VectorZero()))
        return false;

    return Vector4NearEqual(Vector4Dot(weights, s_SIMDOne), s_SIMDOne, VectorReplicate(s_SkinWeightSumEpsilon));
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
    const SIMDVector row0 = matrix.v[0];
    const SIMDVector row1 = matrix.v[1];
    const SIMDVector row2 = matrix.v[2];
    const SIMDVector row3 = matrix.v[3];
    if(
        !VectorIsFinite(row0, 0xFu)
        || !VectorIsFinite(row1, 0xFu)
        || !VectorIsFinite(row2, 0xFu)
        || !VectorIsFinite(row3, 0xFu)
        || !Vector4NearEqual(row3, s_SIMDIdentityR3, VectorReplicate(s_Epsilon))
    )
        return false;

    const SIMDVector determinant = Vector3Dot(
        VectorSetW(row0, 0.0f),
        Vector3Cross(VectorSetW(row1, 0.0f), VectorSetW(row2, 0.0f))
    );
    return VectorIsFinite(determinant, 0xFu) && Vector4Greater(VectorAbs(determinant), VectorReplicate(s_Epsilon));
}

[[nodiscard]] inline bool ValidInverseBindMatrixCount(
    const usize inverseBindMatrixCount,
    const u32 skeletonJointCount){
    if(inverseBindMatrixCount == 0u)
        return true;

    return skeletonJointCount != 0u && inverseBindMatrixCount == static_cast<usize>(skeletonJointCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

