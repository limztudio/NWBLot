// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkeletonRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_Epsilon = 0.000001f;
static constexpr f32 s_JointDeterminantEpsilon = 0.000000000001f;
static constexpr f32 s_RigidJointEpsilon = 0.001f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool HasSkeletonPose(const SkeletonPoseComponent* pose){
    return pose && (!pose->localJoints.empty() || !pose->parentJoints.empty());
}

[[nodiscard]] NWB_INLINE SIMDMatrix MultiplyJointMatrices(const SIMDMatrix& lhs, const SIMDMatrix& rhs){
    return MatrixMultiply(lhs, rhs);
}

[[nodiscard]] NWB_INLINE bool IsAffineJointMatrix(const SIMDMatrix& matrix){
    return
        VectorIsFinite(matrix.v[0], 0xFu)
        && VectorIsFinite(matrix.v[1], 0xFu)
        && VectorIsFinite(matrix.v[2], 0xFu)
        && VectorIsFinite(matrix.v[3], 0xFu)
        && Vector4NearEqual(matrix.v[3], s_SIMDIdentityR3, VectorReplicate(s_Epsilon))
    ;
}

[[nodiscard]] NWB_INLINE f32 JointLinearDeterminant(const SIMDMatrix& matrix){
    const SIMDVector row0 = VectorSetW(matrix.v[0], 0.0f);
    const SIMDVector row1 = VectorSetW(matrix.v[1], 0.0f);
    const SIMDVector row2 = VectorSetW(matrix.v[2], 0.0f);
    return VectorGetX(Vector3Dot(row0, Vector3Cross(row1, row2)));
}

[[nodiscard]] NWB_INLINE bool IsInvertibleAffineJointMatrix(const SIMDMatrix& matrix){
    if(!IsAffineJointMatrix(matrix))
        return false;

    const f32 determinant = JointLinearDeterminant(matrix);
    return IsFinite(determinant) && Abs(determinant) > s_JointDeterminantEpsilon;
}

[[nodiscard]] NWB_INLINE bool ResolveSkinningJointMatrix(
    const SIMDMatrix& poseJoint,
    const bool hasInverseBind,
    const SIMDMatrix& inverseBind,
    SIMDMatrix& outMatrix){
    outMatrix = poseJoint;
    if(!IsInvertibleAffineJointMatrix(outMatrix))
        return false;
    if(!hasInverseBind)
        return true;
    if(!IsInvertibleAffineJointMatrix(inverseBind))
        return false;

    outMatrix = MultiplyJointMatrices(outMatrix, inverseBind);
    return IsInvertibleAffineJointMatrix(outMatrix);
}

[[nodiscard]] NWB_INLINE bool ResolveSkeletonPoseJointMatrix(
    const SIMDMatrix& localJoint,
    const SIMDMatrix* parentJoint,
    SIMDMatrix& outMatrix
){
    outMatrix = localJoint;
    if(!IsInvertibleAffineJointMatrix(outMatrix))
        return false;

    if(parentJoint){
        outMatrix = MultiplyJointMatrices(*parentJoint, outMatrix);
        if(!IsInvertibleAffineJointMatrix(outMatrix))
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
        SIMDMatrix parentJointMatrix;
        const SIMDMatrix* parentJointMatrixPtr = nullptr;
        if(parentJoint != s_SkeletonRootParent){
            if(parentJoint >= jointIndex)
                return false;

            parentJointMatrix = LoadFloat(outJointPalette[parentJoint]);
            parentJointMatrixPtr = &parentJointMatrix;
        }

        SIMDMatrix jointMatrix;
        if(!ResolveSkeletonPoseJointMatrix(LoadFloat(pose.localJoints[jointIndex]), parentJointMatrixPtr, jointMatrix))
            return false;

        SkeletonJointMatrix storedJointMatrix{};
        StoreFloat(jointMatrix, &storedJointMatrix);
        outJointPalette.push_back(storedJointMatrix);
    }

    outSkinningMode = pose.skinningMode;
    return true;
}

[[nodiscard]] inline bool IsRigidJointMatrix(const SIMDMatrix& matrix){
    if(!IsAffineJointMatrix(matrix))
        return false;

    const SIMDVector row0 = VectorSetW(matrix.v[0], 0.0f);
    const SIMDVector row1 = VectorSetW(matrix.v[1], 0.0f);
    const SIMDVector row2 = VectorSetW(matrix.v[2], 0.0f);
    const f32 length0 = VectorGetX(Vector3LengthSq(row0));
    const f32 length1 = VectorGetX(Vector3LengthSq(row1));
    const f32 length2 = VectorGetX(Vector3LengthSq(row2));
    const f32 dot01 = VectorGetX(Vector3Dot(row0, row1));
    const f32 dot02 = VectorGetX(Vector3Dot(row0, row2));
    const f32 dot12 = VectorGetX(Vector3Dot(row1, row2));
    const f32 determinant = VectorGetX(Vector3Dot(row0, Vector3Cross(row1, row2)));
    return
        IsFinite(length0)
        && IsFinite(length1)
        && IsFinite(length2)
        && IsFinite(dot01)
        && IsFinite(dot02)
        && IsFinite(dot12)
        && IsFinite(determinant)
        && Abs(length0 - 1.0f) <= s_RigidJointEpsilon
        && Abs(length1 - 1.0f) <= s_RigidJointEpsilon
        && Abs(length2 - 1.0f) <= s_RigidJointEpsilon
        && Abs(dot01) <= s_RigidJointEpsilon
        && Abs(dot02) <= s_RigidJointEpsilon
        && Abs(dot12) <= s_RigidJointEpsilon
        && Abs(determinant - 1.0f) <= s_RigidJointEpsilon
    ;
}

[[nodiscard]] inline bool TryBuildJointRotationQuaternion(const SIMDMatrix& matrix, SIMDVector& outQuaternion){
    outQuaternion = QuaternionIdentity();
    if(!IsRigidJointMatrix(matrix))
        return false;

    outQuaternion = QuaternionRotationMatrix(MatrixTranspose(matrix));
    return !QuaternionIsNaN(outQuaternion) && !QuaternionIsInfinite(outQuaternion);
}

[[nodiscard]] inline bool TryBuildJointDualQuaternion(const SIMDMatrix& matrix, SIMDVector& outReal, SIMDVector& outDual){
    outReal = QuaternionIdentity();
    outDual = VectorZero();
    if(!TryBuildJointRotationQuaternion(matrix, outReal))
        return false;

    const SIMDVector translation = VectorSet(
        VectorGetW(matrix.v[0]),
        VectorGetW(matrix.v[1]),
        VectorGetW(matrix.v[2]),
        0.0f
    );
    outDual = VectorScale(QuaternionMultiply(translation, outReal), 0.5f);
    return VectorIsFinite(outDual, 0xFu);
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

