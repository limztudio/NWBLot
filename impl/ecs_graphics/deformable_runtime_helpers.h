// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_runtime_mesh_cache.h"

#include <impl/assets_graphics/deformable_geometry_validation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeformableRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_Epsilon = 0.000001f;
static constexpr f32 s_RigidJointEpsilon = 0.001f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool HasMorphWeights(const DeformableMorphWeightsComponent* weights){
    return weights && !weights->weights.empty();
}

[[nodiscard]] inline bool HasSkeletonPose(const DeformableSkeletonPoseComponent* pose){
    return pose && (!pose->localJoints.empty() || !pose->parentJoints.empty());
}

template<typename MorphVector, typename MorphWeightLookup>
[[nodiscard]] inline bool BuildMorphWeightSumLookup(const MorphVector& morphs, const DeformableMorphWeightsComponent* weights, MorphWeightLookup& outWeights, Name& outFailedMorph){
    outWeights.clear();
    outFailedMorph = NAME_NONE;

    if(!HasMorphWeights(weights))
        return true;

    outWeights.reserve(morphs.size());
    for(const DeformableMorph& morph : morphs){
        if(!morph.name)
            continue;

        outWeights.emplace(morph.name.hash(), 0.0f);
    }
    if(outWeights.empty())
        return true;

    for(const DeformableMorphWeight& weight : weights->weights){
        auto iterWeight = outWeights.find(weight.morph.hash());
        if(iterWeight == outWeights.end())
            continue;
        if(!IsFinite(weight.weight)){
            outFailedMorph = weight.morph;
            return false;
        }

        const f32 resolvedWeight = iterWeight.value() + weight.weight;
        if(!IsFinite(resolvedWeight)){
            outFailedMorph = weight.morph;
            return false;
        }
        iterWeight.value() = resolvedWeight;
    }
    return true;
}

[[nodiscard]] inline SIMDMatrix LoadJointMatrix(const DeformableJointMatrix& matrix){
    return LoadFloat(matrix);
}

[[nodiscard]] inline DeformableJointMatrix StoreJointMatrix(const SIMDMatrix& matrix){
    DeformableJointMatrix result{};
    StoreFloat(matrix, &result);
    return result;
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
        DeformableValidation::FiniteVector(matrix.v[0], 0xFu)
        && DeformableValidation::FiniteVector(matrix.v[1], 0xFu)
        && DeformableValidation::FiniteVector(matrix.v[2], 0xFu)
        && DeformableValidation::FiniteVector(matrix.v[3], 0xFu)
        && Vector4NearEqual(affineW, s_SIMDIdentityR3, VectorReplicate(DeformableValidation::s_Epsilon))
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
    return IsFinite(determinant) && Abs(determinant) > s_Epsilon;
}

[[nodiscard]] inline bool ResolveSkinningJointMatrix(
    const DeformableRuntimeMeshInstance& instance,
    const u32 jointIndex,
    const DeformableJointMatrix& poseJoint,
    SIMDMatrix& outMatrix){
    outMatrix = LoadJointMatrix(poseJoint);
    if(!IsInvertibleAffineJointMatrix(outMatrix))
        return false;
    if(instance.inverseBindMatrices.empty())
        return true;
    if(jointIndex >= instance.inverseBindMatrices.size())
        return false;

    const SIMDMatrix inverseBind = LoadJointMatrix(instance.inverseBindMatrices[jointIndex]);
    if(!IsInvertibleAffineJointMatrix(inverseBind))
        return false;

    outMatrix = MultiplyJointMatrices(outMatrix, inverseBind);
    return IsInvertibleAffineJointMatrix(outMatrix);
}

template<typename JointMatrixVector>
[[nodiscard]] inline bool BuildJointPaletteFromSkeletonPose(
    const DeformableSkeletonPoseComponent& pose,
    JointMatrixVector& outJointPalette,
    u32& outSkinningMode){
    outJointPalette.clear();
    outSkinningMode = DeformableSkinningMode::LinearBlend;

    if(!HasSkeletonPose(&pose))
        return true;
    if(!ValidDeformableSkinningMode(pose.skinningMode))
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
        SIMDMatrix jointMatrix = LoadJointMatrix(pose.localJoints[jointIndex]);
        if(!IsInvertibleAffineJointMatrix(jointMatrix))
            return false;

        if(parentJoint != s_DeformableSkeletonRootParent){
            if(parentJoint >= jointIndex)
                return false;

            jointMatrix = MultiplyJointMatrices(LoadJointMatrix(outJointPalette[parentJoint]), jointMatrix);
            if(!IsInvertibleAffineJointMatrix(jointMatrix))
                return false;
        }

        outJointPalette.push_back(StoreJointMatrix(jointMatrix));
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
    return
        DeformableValidation::FiniteVector(outNormalMatrix.v[0], 0x7u)
        && DeformableValidation::FiniteVector(outNormalMatrix.v[1], 0x7u)
        && DeformableValidation::FiniteVector(outNormalMatrix.v[2], 0x7u)
    ;
}

[[nodiscard]] inline bool TryTransformJointNormalDirection(const SIMDMatrix& matrix, const SIMDVector directionVector, SIMDVector& outDirection){
    SIMDMatrix normalMatrix;
    if(!TryBuildJointNormalMatrix(matrix, normalMatrix))
        return false;

    SIMDVector result = VectorMultiply(VectorSplatX(directionVector), normalMatrix.v[0]);
    result = VectorMultiplyAdd(VectorSplatY(directionVector), normalMatrix.v[1], result);
    result = VectorMultiplyAdd(VectorSplatZ(directionVector), normalMatrix.v[2], result);
    outDirection = VectorSetW(result, 0.0f);
    return DeformableValidation::FiniteVector(outDirection, 0x7u);
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
    return DeformableValidation::FiniteVector(outDual, 0xFu);
}

[[nodiscard]] inline DeformableJointMatrix StoreJointDualQuaternionPayload(const SIMDVector real, const SIMDVector dual){
    DeformableJointMatrix result{};
    StoreFloat(real, &result.rows[0]);
    StoreFloat(dual, &result.rows[1]);
    return result;
}

[[nodiscard]] inline bool NormalizeBlendedDualQuaternion(SIMDVector& real, SIMDVector& dual){
    const f32 lengthSquared = VectorGetX(QuaternionLengthSq(real));
    if(!IsFinite(lengthSquared) || lengthSquared <= s_Epsilon)
        return false;

    const f32 invLength = 1.0f / Sqrt(lengthSquared);
    real = VectorScale(real, invLength);
    dual = VectorScale(dual, invLength);
    dual = VectorSubtract(dual, VectorScale(real, VectorGetX(Vector4Dot(real, dual))));
    return
        DeformableValidation::FiniteVector(real, 0xFu)
        && DeformableValidation::FiniteVector(dual, 0xFu)
    ;
}

[[nodiscard]] inline SIMDVector TransformDualQuaternionPosition(const SIMDVector real, const SIMDVector dual, const SIMDVector position){
    const SIMDVector rotatedPosition = Vector3Rotate(position, real);
    const SIMDVector translation = VectorScale(QuaternionMultiply(dual, QuaternionConjugate(real)), 2.0f);
    return VectorAdd(rotatedPosition, translation);
}

[[nodiscard]] inline SIMDVector TransformDualQuaternionDirection(const SIMDVector real, const SIMDVector direction){
    return Vector3Rotate(direction, real);
}

[[nodiscard]] inline bool ValidRuntimeMeshPayloadArrays(const DeformableRuntimeMeshInstance& instance){
    return DeformableValidation::ValidRuntimePayloadArrays(
        instance.restVertices,
        instance.indices,
        instance.sourceTriangleCount,
        instance.skeletonJointCount,
        instance.skin,
        instance.inverseBindMatrices,
        instance.sourceSamples,
        instance.editMaskPerTriangle,
        instance.morphs
    );
}

[[nodiscard]] inline bool ValidateTriangleIndex(const DeformableRuntimeMeshInstance& instance, const u32 triangle, u32 (&outIndices)[3]){
    const usize indexBase = static_cast<usize>(triangle) * 3u;
    if(indexBase > instance.indices.size() || instance.indices.size() - indexBase < 3u)
        return false;

    outIndices[0] = instance.indices[indexBase + 0u];
    outIndices[1] = instance.indices[indexBase + 1u];
    outIndices[2] = instance.indices[indexBase + 2u];
    return
        outIndices[0] < instance.restVertices.size()
        && outIndices[1] < instance.restVertices.size()
        && outIndices[2] < instance.restVertices.size()
    ;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

