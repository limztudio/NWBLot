// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkeletonRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_AffineEpsilon = 0.000001f;
static constexpr f32 s_JointDeterminantEpsilon = 0.000000000001f;
static constexpr f32 s_RigidJointEpsilon = 0.001f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool HasSkeletonPose(const SkeletonPoseComponent* pose){
    return pose && (!pose->localJoints.empty() || !pose->parentJoints.empty());
}

[[nodiscard]] NWB_INLINE bool ResolveSkinningJointMatrix(
    const SIMDMatrix& poseJoint,
    const bool hasInverseBind,
    const SIMDMatrix& inverseBind,
    SIMDMatrix& outMatrix){
    outMatrix = poseJoint;
    if(!MatrixIsInvertibleAffine(outMatrix, s_AffineEpsilon, s_JointDeterminantEpsilon))
        return false;
    if(!hasInverseBind)
        return true;
    if(!MatrixIsInvertibleAffine(inverseBind, s_AffineEpsilon, s_JointDeterminantEpsilon))
        return false;

    outMatrix = MatrixMultiply(outMatrix, inverseBind);
    return MatrixIsInvertibleAffine(outMatrix, s_AffineEpsilon, s_JointDeterminantEpsilon);
}

[[nodiscard]] NWB_INLINE bool ResolveSkeletonPoseJointMatrix(
    const SIMDMatrix& localJoint,
    const SIMDMatrix* parentJoint,
    SIMDMatrix& outMatrix
){
    outMatrix = localJoint;
    if(!MatrixIsInvertibleAffine(outMatrix, s_AffineEpsilon, s_JointDeterminantEpsilon))
        return false;

    if(parentJoint){
        outMatrix = MatrixMultiply(*parentJoint, outMatrix);
        if(!MatrixIsInvertibleAffine(outMatrix, s_AffineEpsilon, s_JointDeterminantEpsilon))
            return false;
    }
    return true;
}

template<typename JointMatrixVector>
[[nodiscard]] inline bool BuildStoredJointPaletteFromSkeletonPose(
    const SkeletonPoseComponent& pose,
    JointMatrixVector& outJointPalette,
    u32& outSkinningMode){
    outJointPalette.clear();
    outSkinningMode = SkeletonSkinningMode::LinearBlend;

    if(!HasSkeletonPose(&pose))
        return true;
    if(!ValidSkeletonSkinningMode(pose.skinningMode))
        return false;

    const usize jointCount = pose.localJoints.size();
    if(
        jointCount == 0u
        || pose.parentJoints.size() != jointCount
        || jointCount > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    outJointPalette.reserve(jointCount);
    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        const u32 parentJoint = pose.parentJoints[jointIndex];
        SIMDMatrix parentJointMatrix{};
        const SIMDMatrix* parentJointMatrixPtr = nullptr;
        if(parentJoint != s_SkeletonRootParent){
            if(parentJoint >= jointIndex)
                return false;

            parentJointMatrix = LoadFloat(outJointPalette[parentJoint]);
            parentJointMatrixPtr = &parentJointMatrix;
        }

        SIMDMatrix resolvedJointMatrix{};
        if(!ResolveSkeletonPoseJointMatrix(
            LoadFloat(pose.localJoints[jointIndex]),
            parentJointMatrixPtr,
            resolvedJointMatrix
        ))
            return false;

        SkeletonJointMatrix storedJointMatrix{};
        StoreFloat(resolvedJointMatrix, &storedJointMatrix);
        outJointPalette.push_back(storedJointMatrix);
    }

    outSkinningMode = pose.skinningMode;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

