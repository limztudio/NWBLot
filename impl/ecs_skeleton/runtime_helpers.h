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


[[nodiscard]] NWB_INLINE bool HasSkeletonPose(const SkeletonPoseComponent* pose)noexcept{
    return pose && (!pose->localJoints.empty() || !pose->parentJoints.empty());
}

[[nodiscard]] NWB_INLINE Expected<SIMDMatrix> ResolveSkinningJointMatrix(
    const SIMDMatrix& poseJoint,
    const bool hasInverseBind,
    const SIMDMatrix& inverseBind,
    const f32 inverseBindDeterminantEpsilon
)noexcept{
    SIMDMatrix matrix = poseJoint;
    if(!MatrixIsInvertibleAffine(matrix, s_AffineEpsilon, s_JointDeterminantEpsilon))
        return MakeUnexpected(Failure{});
    if(!hasInverseBind)
        return matrix;
    if(!MatrixIsInvertibleAffine(inverseBind, s_AffineEpsilon, inverseBindDeterminantEpsilon))
        return MakeUnexpected(Failure{});

    matrix = MatrixMultiply(matrix, inverseBind);
    if(!MatrixIsInvertibleAffine(matrix, s_AffineEpsilon, s_JointDeterminantEpsilon))
        return MakeUnexpected(Failure{});
    return matrix;
}

[[nodiscard]] NWB_INLINE Expected<SIMDMatrix> ResolveSkeletonPoseJointMatrix(
    const SIMDMatrix& localJoint,
    const SIMDMatrix* parentJoint
)noexcept{
    SIMDMatrix matrix = localJoint;
    if(!MatrixIsInvertibleAffine(matrix, s_AffineEpsilon, s_JointDeterminantEpsilon))
        return MakeUnexpected(Failure{});

    if(parentJoint){
        matrix = MatrixMultiply(*parentJoint, matrix);
        if(!MatrixIsInvertibleAffine(matrix, s_AffineEpsilon, s_JointDeterminantEpsilon))
            return MakeUnexpected(Failure{});
    }
    return matrix;
}

template<typename JointMatrixVector>
[[nodiscard]] inline Expected<u32> BuildStoredJointPaletteFromSkeletonPose(
    const SkeletonPoseComponent& pose,
    JointMatrixVector& outJointPalette
){
    outJointPalette.clear();

    if(!HasSkeletonPose(&pose))
        return static_cast<u32>(SkeletonSkinningMode::LinearBlend);
    if(!ValidSkeletonSkinningMode(pose.skinningMode))
        return MakeUnexpected(Failure{});

    const usize jointCount = pose.localJoints.size();
    if(
        jointCount == 0u
        || pose.parentJoints.size() != jointCount
        || jointCount > static_cast<usize>(Limit<u32>::s_Max)
    )
        return MakeUnexpected(Failure{});

    outJointPalette.reserve(jointCount);
    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        const u32 parentJoint = pose.parentJoints[jointIndex];
        SIMDMatrix parentJointMatrix{};
        const SIMDMatrix* parentJointMatrixPtr = nullptr;
        if(parentJoint != s_SkeletonRootParent){
            if(parentJoint >= jointIndex)
                return MakeUnexpected(Failure{});

            parentJointMatrix = LoadFloat(outJointPalette[parentJoint]);
            parentJointMatrixPtr = &parentJointMatrix;
        }

        const auto resolvedJointMatrix = ResolveSkeletonPoseJointMatrix(LoadFloat(pose.localJoints[jointIndex]), parentJointMatrixPtr);
        if(!resolvedJointMatrix)
            return MakeUnexpected(Failure{});

        SkeletonJointMatrix storedJointMatrix{};
        StoreFloat(*resolvedJointMatrix, storedJointMatrix);
        outJointPalette.push_back(storedJointMatrix);
    }

    return pose.skinningMode;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

