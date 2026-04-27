// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_runtime_mesh_cache.h"

#include <core/geometry/frame_math.h>
#include <impl/assets_graphics/deformable_geometry_validation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeformableRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_Epsilon = 0.000001f;
static constexpr f32 s_RigidJointEpsilon = 0.001f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DisplacementResolveFailure{
    enum Enum : u8{
        None,
        Descriptor,
        Scale,
        Amplitude,
    };
};

[[nodiscard]] inline bool HasMorphWeights(const DeformableMorphWeightsComponent* weights){
    return weights && !weights->weights.empty();
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
    SIMDMatrix result{};
    result.v[0] = LoadFloat(matrix.column0);
    result.v[1] = LoadFloat(matrix.column1);
    result.v[2] = LoadFloat(matrix.column2);
    result.v[3] = LoadFloat(matrix.column3);
    return result;
}

[[nodiscard]] inline DeformableJointMatrix StoreJointMatrix(const SIMDMatrix& matrix){
    DeformableJointMatrix result;
    StoreFloat(matrix.v[0], &result.column0);
    StoreFloat(matrix.v[1], &result.column1);
    StoreFloat(matrix.v[2], &result.column2);
    StoreFloat(matrix.v[3], &result.column3);
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
    return DeformableValidation::FiniteVector(matrix.v[0], 0xFu)
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
    SIMDMatrix& outMatrix)
{
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
    return DeformableValidation::FiniteVector(outNormalMatrix.v[0], 0x7u)
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
    return IsFinite(length0)
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

    const f32 m00 = VectorGetX(matrix.v[0]);
    const f32 m10 = VectorGetY(matrix.v[0]);
    const f32 m20 = VectorGetZ(matrix.v[0]);
    const f32 m01 = VectorGetX(matrix.v[1]);
    const f32 m11 = VectorGetY(matrix.v[1]);
    const f32 m21 = VectorGetZ(matrix.v[1]);
    const f32 m02 = VectorGetX(matrix.v[2]);
    const f32 m12 = VectorGetY(matrix.v[2]);
    const f32 m22 = VectorGetZ(matrix.v[2]);

    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 1.0f;
    const f32 trace = m00 + m11 + m22;
    if(trace > 0.0f){
        const f32 s = Sqrt(trace + 1.0f) * 2.0f;
        if(!IsFinite(s) || s <= s_Epsilon)
            return false;

        x = (m21 - m12) / s;
        y = (m02 - m20) / s;
        z = (m10 - m01) / s;
        w = 0.25f * s;
    }
    else if(m00 > m11 && m00 > m22){
        const f32 s = Sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        if(!IsFinite(s) || s <= s_Epsilon)
            return false;

        x = 0.25f * s;
        y = (m01 + m10) / s;
        z = (m02 + m20) / s;
        w = (m21 - m12) / s;
    }
    else if(m11 > m22){
        const f32 s = Sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        if(!IsFinite(s) || s <= s_Epsilon)
            return false;

        x = (m01 + m10) / s;
        y = 0.25f * s;
        z = (m12 + m21) / s;
        w = (m02 - m20) / s;
    }
    else{
        const f32 s = Sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        if(!IsFinite(s) || s <= s_Epsilon)
            return false;

        x = (m02 + m20) / s;
        y = (m12 + m21) / s;
        z = 0.25f * s;
        w = (m10 - m01) / s;
    }

    const SIMDVector quaternion = VectorSet(x, y, z, w);
    if(QuaternionIsNaN(quaternion) || QuaternionIsInfinite(quaternion))
        return false;

    outQuaternion = QuaternionNormalize(quaternion);
    return !QuaternionIsNaN(outQuaternion) && !QuaternionIsInfinite(outQuaternion);
}

[[nodiscard]] inline bool TryBuildJointDualQuaternion(
    const SIMDMatrix& matrix,
    SIMDVector& outReal,
    SIMDVector& outDual)
{
    outReal = QuaternionIdentity();
    outDual = VectorZero();
    if(!TryBuildJointRotationQuaternion(matrix, outReal))
        return false;

    const SIMDVector translation = VectorSetW(matrix.v[3], 0.0f);
    outDual = VectorScale(QuaternionMultiply(translation, outReal), 0.5f);
    return DeformableValidation::FiniteVector(outDual, 0xFu);
}

[[nodiscard]] inline DeformableJointMatrix StoreJointDualQuaternionPayload(
    const SIMDVector real,
    const SIMDVector dual)
{
    DeformableJointMatrix result{};
    StoreFloat(real, &result.column0);
    StoreFloat(dual, &result.column1);
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
    return DeformableValidation::FiniteVector(real, 0xFu)
        && DeformableValidation::FiniteVector(dual, 0xFu)
    ;
}

[[nodiscard]] inline SIMDVector TransformDualQuaternionPosition(
    const SIMDVector real,
    const SIMDVector dual,
    const SIMDVector position)
{
    const SIMDVector rotatedPosition = Vector3Rotate(position, real);
    const SIMDVector translation = VectorScale(QuaternionMultiply(dual, QuaternionConjugate(real)), 2.0f);
    return VectorAdd(rotatedPosition, translation);
}

[[nodiscard]] inline SIMDVector TransformDualQuaternionDirection(const SIMDVector real, const SIMDVector direction){
    return Vector3Rotate(direction, real);
}

[[nodiscard]] inline bool ValidateTriangleIndex(const DeformableRuntimeMeshInstance& instance, const u32 triangle, u32 (&outIndices)[3]){
    const usize indexBase = static_cast<usize>(triangle) * 3u;
    if(indexBase > instance.indices.size() || instance.indices.size() - indexBase < 3u)
        return false;

    outIndices[0] = instance.indices[indexBase + 0u];
    outIndices[1] = instance.indices[indexBase + 1u];
    outIndices[2] = instance.indices[indexBase + 2u];
    return outIndices[0] < instance.restVertices.size()
        && outIndices[1] < instance.restVertices.size()
        && outIndices[2] < instance.restVertices.size()
    ;
}

[[nodiscard]] inline bool ResolveEffectiveDisplacement(const DeformableDisplacement& sourceDisplacement, const DeformableDisplacementComponent* component, DeformableDisplacement& outDisplacement, DisplacementResolveFailure::Enum& outFailure){
    outDisplacement = sourceDisplacement;
    outFailure = DisplacementResolveFailure::None;

    if(!ValidDeformableDisplacementDescriptor(outDisplacement)){
        outFailure = DisplacementResolveFailure::Descriptor;
        return false;
    }
    if(outDisplacement.mode == DeformableDisplacementMode::None)
        return true;
    if(component && !component->enabled){
        outDisplacement = DeformableDisplacement{};
        return true;
    }

    const f32 scale = component ? component->amplitudeScale : 1.0f;
    if(!IsFinite(scale)){
        outFailure = DisplacementResolveFailure::Scale;
        return false;
    }

    outDisplacement.amplitude *= scale;
    if(!IsFinite(outDisplacement.amplitude)){
        outFailure = DisplacementResolveFailure::Amplitude;
        return false;
    }
    if(!DeformableValidation::ActiveWeight(outDisplacement.amplitude))
        outDisplacement = DeformableDisplacement{};
    return true;
}

[[nodiscard]] inline bool ResolveEffectiveDisplacement(const DeformableDisplacement& sourceDisplacement, const DeformableDisplacementComponent* component, DeformableDisplacement& outDisplacement){
    DisplacementResolveFailure::Enum failure = DisplacementResolveFailure::None;
    return ResolveEffectiveDisplacement(sourceDisplacement, component, outDisplacement, failure);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

