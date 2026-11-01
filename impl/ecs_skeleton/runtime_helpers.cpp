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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

