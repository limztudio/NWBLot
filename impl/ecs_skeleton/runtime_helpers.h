// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_skeleton/components.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkeletonRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<SIMDMatrix> ResolveSkinningJointMatrix(const SIMDMatrix& poseJoint, const b8 hasInverseBind, const SIMDMatrix& inverseBind, const f32 inverseBindDeterminantEpsilon)noexcept{
    SIMDMatrix matrix = poseJoint;
    if(!MatrixIsInvertibleAffine(matrix, s_SkeletonAffineEpsilon, s_SkeletonJointDeterminantEpsilon))
        return MakeUnexpected(Failure{});
    if(!hasInverseBind)
        return matrix;
    if(!MatrixIsInvertibleAffine(inverseBind, s_SkeletonAffineEpsilon, inverseBindDeterminantEpsilon))
        return MakeUnexpected(Failure{});

    matrix = MatrixMultiply(matrix, inverseBind);
    if(!MatrixIsInvertibleAffine(matrix, s_SkeletonAffineEpsilon, s_SkeletonJointDeterminantEpsilon))
        return MakeUnexpected(Failure{});
    return matrix;
}

[[nodiscard]] inline Expected<SIMDMatrix> ResolveSkeletonPoseJointMatrix(const SIMDMatrix& localJoint, const SIMDMatrix* parentJoint)noexcept{
    SIMDMatrix matrix = localJoint;
    if(!MatrixIsInvertibleAffine(matrix, s_SkeletonAffineEpsilon, s_SkeletonJointDeterminantEpsilon))
        return MakeUnexpected(Failure{});

    if(parentJoint){
        matrix = MatrixMultiply(*parentJoint, matrix);
        if(!MatrixIsInvertibleAffine(matrix, s_SkeletonAffineEpsilon, s_SkeletonJointDeterminantEpsilon))
            return MakeUnexpected(Failure{});
    }
    return matrix;
}

[[nodiscard]] inline Expected<SIMDMatrix> EvaluateSkeletonPoseJointMatrix(const SkeletonPoseComponent& pose, const u32 targetJointIndex)noexcept{
    return TryEvaluateSkeletonPoseJointMatrix(pose, targetJointIndex);
}

template<typename JointMatrixVector>
[[nodiscard]] inline Expected<u32> BuildStoredJointPaletteFromSkeletonPose(const SkeletonPoseComponent& pose, JointMatrixVector& outJointPalette)noexcept(
    noexcept(outJointPalette.clear())
    && noexcept(outJointPalette.reserve(DeclVal<const usize&>()))
    && noexcept(DeclVal<SIMDMatrix&>() = LoadFloat(outJointPalette[DeclVal<const u32&>()]))
    && noexcept(outJointPalette.push_back(DeclVal<SkeletonJointMatrix&>()))
){
    outJointPalette.clear();

    if(!HasSkeletonPoseData(pose))
        return static_cast<u32>(SkeletonSkinningMode::LinearBlend);
    if(!ValidSkeletonPoseTopology(pose))
        return MakeUnexpected(Failure{});

    const usize jointCount = pose.localJoints.size();

    outJointPalette.reserve(jointCount);
    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        const u32 parentJoint = pose.parentJoints[jointIndex];
        SIMDMatrix parentJointMatrix{};
        const SIMDMatrix* parentJointMatrixPtr = nullptr;
        if(parentJoint != s_SkeletonInvalidJointIndex){
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

