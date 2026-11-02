// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkeletonRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SIMDMatrix> ResolveSkinningJointMatrix(const SIMDMatrix& poseJoint, const b8 hasInverseBind, const SIMDMatrix& inverseBind, const f32 inverseBindDeterminantEpsilon)noexcept{
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

Expected<SIMDMatrix> ResolveSkeletonPoseJointMatrix(const SIMDMatrix& localJoint, const SIMDMatrix* parentJoint)noexcept{
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

Expected<SIMDMatrix> EvaluateSkeletonPoseJointMatrix(const SkeletonPoseComponent& pose, const u32 targetJointIndex)noexcept{
    if(!HasSkeletonPoseData(pose) || !ValidSkeletonPoseTopology(pose))
        return MakeUnexpected(Failure{});

    const usize jointCount = pose.localJoints.size();
    if(static_cast<usize>(targetJointIndex) >= jointCount)
        return MakeUnexpected(Failure{});

    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        if(!MatrixIsInvertibleAffine(LoadFloat(pose.localJoints[jointIndex]), s_SkeletonAffineEpsilon, s_SkeletonJointDeterminantEpsilon))
            return MakeUnexpected(Failure{});
    }

    SIMDMatrix matrix = LoadFloat(pose.localJoints[targetJointIndex]);
    u32 currentJoint = targetJointIndex;
    while(pose.parentJoints[currentJoint] != s_SkeletonInvalidJointIndex){
        const u32 parentJoint = pose.parentJoints[currentJoint];
        matrix = MatrixMultiply(LoadFloat(pose.localJoints[parentJoint]), matrix);
        if(!MatrixIsInvertibleAffine(matrix, s_SkeletonAffineEpsilon, s_SkeletonJointDeterminantEpsilon))
            return MakeUnexpected(Failure{});
        currentJoint = parentJoint;
    }
    return matrix;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

