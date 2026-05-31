// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinnedMeshRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_Epsilon = static_cast<f32>(NWB_SKINNED_MESH_EPSILON);
static constexpr f32 s_JointDeterminantEpsilon = 0.000000000001f;
static constexpr f32 s_RigidJointEpsilon = 0.001f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ActiveWeight(const f32 value){
    return value > s_Epsilon || value < -s_Epsilon;
}

[[nodiscard]] inline bool FiniteVector(const SIMDVector value, const u32 activeMask){
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    return (VectorMoveMask(invalid) & activeMask) == 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool HasSkeletonPose(const SkinnedMeshSkeletonPoseComponent* pose){
    return pose && (!pose->localJoints.empty() || !pose->parentJoints.empty());
}

[[nodiscard]] inline SIMDVector TransformJointColumn(const SIMDMatrix& matrix, const SIMDVector column){
    SIMDVector result = VectorMultiply(VectorSplatX(column), matrix.v[0]);
    result = VectorMultiplyAdd(VectorSplatY(column), matrix.v[1], result);
    result = VectorMultiplyAdd(VectorSplatZ(column), matrix.v[2], result);
    return VectorMultiplyAdd(VectorSplatW(column), matrix.v[3], result);
}

[[nodiscard]] inline SIMDMatrix MultiplyJointMatrices(const SIMDMatrix& lhs, const SIMDMatrix& rhs){
    SIMDMatrix result{};
    result.v[0] = TransformJointColumn(lhs, rhs.v[0]);
    result.v[1] = TransformJointColumn(lhs, rhs.v[1]);
    result.v[2] = TransformJointColumn(lhs, rhs.v[2]);
    result.v[3] = TransformJointColumn(lhs, rhs.v[3]);
    return result;
}

[[nodiscard]] inline bool IsAffineJointMatrix(const SIMDMatrix& matrix){
    const SIMDVector affineW = VectorSet(
        VectorGetW(matrix.v[0]),
        VectorGetW(matrix.v[1]),
        VectorGetW(matrix.v[2]),
        VectorGetW(matrix.v[3])
    );
    return
        FiniteVector(matrix.v[0], 0xFu)
        && FiniteVector(matrix.v[1], 0xFu)
        && FiniteVector(matrix.v[2], 0xFu)
        && FiniteVector(matrix.v[3], 0xFu)
        && Vector4NearEqual(affineW, s_SIMDIdentityR3, VectorReplicate(s_Epsilon))
    ;
}

[[nodiscard]] inline f32 JointLinearDeterminant(const SIMDMatrix& matrix){
    const SIMDVector column0 = VectorSetW(matrix.v[0], 0.0f);
    const SIMDVector column1 = VectorSetW(matrix.v[1], 0.0f);
    const SIMDVector column2 = VectorSetW(matrix.v[2], 0.0f);
    return VectorGetX(Vector3Dot(column0, Vector3Cross(column1, column2)));
}

[[nodiscard]] inline bool IsInvertibleAffineJointMatrix(const SIMDMatrix& matrix){
    if(!IsAffineJointMatrix(matrix))
        return false;

    const f32 determinant = JointLinearDeterminant(matrix);
    return IsFinite(determinant) && Abs(determinant) > s_JointDeterminantEpsilon;
}

[[nodiscard]] inline bool ResolveSkinningJointMatrix(
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

template<typename JointMatrixVector>
[[nodiscard]] inline bool BuildJointPaletteFromSkeletonPose(
    const SkinnedMeshSkeletonPoseComponent& pose,
    JointMatrixVector& outJointPalette,
    u32& outSkinningMode){
    outJointPalette.clear();
    outSkinningMode = SkinnedMeshSkinningMode::LinearBlend;

    if(!HasSkeletonPose(&pose))
        return true;
    if(!ValidSkinnedMeshSkinningMode(pose.skinningMode))
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
        SIMDMatrix jointMatrix = LoadFloat(pose.localJoints[jointIndex]);
        if(!IsInvertibleAffineJointMatrix(jointMatrix))
            return false;

        if(parentJoint != s_SkinnedMeshSkeletonRootParent){
            if(parentJoint >= jointIndex)
                return false;

            jointMatrix = MultiplyJointMatrices(LoadFloat(outJointPalette[parentJoint]), jointMatrix);
            if(!IsInvertibleAffineJointMatrix(jointMatrix))
                return false;
        }

        SkinnedMeshJointMatrix storedJointMatrix{};
        StoreFloat(jointMatrix, &storedJointMatrix);
        outJointPalette.push_back(storedJointMatrix);
    }

    outSkinningMode = pose.skinningMode;
    return true;
}

[[nodiscard]] inline bool TryBuildJointNormalMatrix(const SIMDMatrix& matrix, SIMDMatrix& outNormalMatrix){
    const SIMDVector column0 = VectorSetW(matrix.v[0], 0.0f);
    const SIMDVector column1 = VectorSetW(matrix.v[1], 0.0f);
    const SIMDVector column2 = VectorSetW(matrix.v[2], 0.0f);
    const SIMDVector cross12 = Vector3Cross(column1, column2);
    const SIMDVector cross20 = Vector3Cross(column2, column0);
    const SIMDVector cross01 = Vector3Cross(column0, column1);
    const f32 determinant = VectorGetX(Vector3Dot(column0, cross12));
    if(!IsFinite(determinant) || Abs(determinant) <= s_Epsilon)
        return false;

    const SIMDVector inverseDeterminant = VectorReplicate(1.0f / determinant);
    outNormalMatrix.v[0] = VectorSetW(VectorMultiply(cross12, inverseDeterminant), 0.0f);
    outNormalMatrix.v[1] = VectorSetW(VectorMultiply(cross20, inverseDeterminant), 0.0f);
    outNormalMatrix.v[2] = VectorSetW(VectorMultiply(cross01, inverseDeterminant), 0.0f);
    outNormalMatrix.v[3] = VectorZero();
    return FiniteVector(outNormalMatrix.v[0], 0x7u) && FiniteVector(outNormalMatrix.v[1], 0x7u) && FiniteVector(outNormalMatrix.v[2], 0x7u);
}

[[nodiscard]] inline bool TryTransformJointNormalDirection(const SIMDMatrix& matrix, const SIMDVector directionVector, SIMDVector& outDirection){
    SIMDMatrix normalMatrix;
    if(!TryBuildJointNormalMatrix(matrix, normalMatrix))
        return false;

    SIMDVector result = VectorMultiply(VectorSplatX(directionVector), normalMatrix.v[0]);
    result = VectorMultiplyAdd(VectorSplatY(directionVector), normalMatrix.v[1], result);
    result = VectorMultiplyAdd(VectorSplatZ(directionVector), normalMatrix.v[2], result);
    outDirection = VectorSetW(result, 0.0f);
    return FiniteVector(outDirection, 0x7u);
}

[[nodiscard]] inline bool IsRigidJointMatrix(const SIMDMatrix& matrix){
    if(!IsAffineJointMatrix(matrix))
        return false;

    const SIMDVector column0 = VectorSetW(matrix.v[0], 0.0f);
    const SIMDVector column1 = VectorSetW(matrix.v[1], 0.0f);
    const SIMDVector column2 = VectorSetW(matrix.v[2], 0.0f);
    const f32 length0 = VectorGetX(Vector3LengthSq(column0));
    const f32 length1 = VectorGetX(Vector3LengthSq(column1));
    const f32 length2 = VectorGetX(Vector3LengthSq(column2));
    const f32 dot01 = VectorGetX(Vector3Dot(column0, column1));
    const f32 dot02 = VectorGetX(Vector3Dot(column0, column2));
    const f32 dot12 = VectorGetX(Vector3Dot(column1, column2));
    const f32 determinant = VectorGetX(Vector3Dot(column0, Vector3Cross(column1, column2)));
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

    const SIMDVector translation = VectorSetW(matrix.v[3], 0.0f);
    outDual = VectorScale(QuaternionMultiply(translation, outReal), 0.5f);
    return FiniteVector(outDual, 0xFu);
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

