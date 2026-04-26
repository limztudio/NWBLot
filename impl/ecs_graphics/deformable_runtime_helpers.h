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
static constexpr f32 s_FrameEpsilon = 0.00000001f;


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

[[nodiscard]] inline SIMDVector Normalize(SIMDVector value, SIMDVector fallback){
    if(!DeformableValidation::FiniteVector(value, 0x7u))
        return fallback;

    const SIMDVector lengthSquaredVector = Vector3LengthSq(value);
    const f32 lengthSquared = VectorGetX(lengthSquaredVector);
    if(!IsFinite(lengthSquared) || lengthSquared <= s_FrameEpsilon)
        return fallback;

    return VectorMultiply(value, VectorReciprocalSqrt(lengthSquaredVector));
}

[[nodiscard]] inline bool ValidFrameDirection(SIMDVector value){
    return DeformableValidation::FiniteVector(value, 0x7u)
        && VectorGetX(Vector3LengthSq(value)) > s_FrameEpsilon
    ;
}

[[nodiscard]] inline SIMDVector ProjectOntoFramePlane(SIMDVector value, SIMDVector normal){
    return VectorMultiplyAdd(
        normal,
        VectorReplicate(-VectorGetX(Vector3Dot(value, normal))),
        value
    );
}

[[nodiscard]] inline SIMDVector FallbackTangent(SIMDVector normal){
    const SIMDVector axis = Abs(VectorGetZ(normal)) < 0.999f
        ? VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
        : VectorSet(0.0f, 1.0f, 0.0f, 0.0f)
    ;
    return Normalize(Vector3Cross(axis, normal), VectorSet(1.0f, 0.0f, 0.0f, 0.0f));
}

[[nodiscard]] inline f32 TangentHandedness(const f32 handedness, const f32 fallbackHandedness){
    if(Abs(handedness) > s_Epsilon)
        return handedness < 0.0f ? -1.0f : 1.0f;
    return fallbackHandedness < 0.0f ? -1.0f : 1.0f;
}

[[nodiscard]] inline SIMDVector ResolveFrameTangent(SIMDVector normal, SIMDVector tangent, SIMDVector fallbackTangent){
    const SIMDVector safeFallbackTangent = FallbackTangent(normal);

    SIMDVector projectedTangent = DeformableValidation::FiniteVector(tangent, 0x7u)
        ? ProjectOntoFramePlane(tangent, normal)
        : safeFallbackTangent
    ;
    if(!ValidFrameDirection(projectedTangent)){
        projectedTangent = DeformableValidation::FiniteVector(fallbackTangent, 0x7u)
            ? ProjectOntoFramePlane(fallbackTangent, normal)
            : safeFallbackTangent
        ;
    }
    if(!ValidFrameDirection(projectedTangent))
        return safeFallbackTangent;

    return Normalize(projectedTangent, safeFallbackTangent);
}

[[nodiscard]] inline SIMDVector ResolveFrameBitangent(SIMDVector normal, SIMDVector tangent, SIMDVector fallbackBitangent){
    const SIMDVector safeFallbackBitangent = Normalize(
        DeformableValidation::FiniteVector(fallbackBitangent, 0x7u)
            ? ProjectOntoFramePlane(fallbackBitangent, normal)
            : Vector3Cross(normal, FallbackTangent(normal)),
        VectorSet(0.0f, 1.0f, 0.0f, 0.0f)
    );

    SIMDVector bitangent = Vector3Cross(normal, tangent);
    if(!ValidFrameDirection(bitangent))
        bitangent = safeFallbackBitangent;
    return Normalize(bitangent, safeFallbackBitangent);
}

inline void OrthonormalizeFrame(SIMDVector& normal, SIMDVector& tangent, const SIMDVector fallbackNormal, const SIMDVector fallbackTangent){
    normal = Normalize(normal, Normalize(fallbackNormal, VectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
    tangent = VectorSetW(
        ResolveFrameTangent(normal, tangent, fallbackTangent),
        TangentHandedness(VectorGetW(tangent), VectorGetW(fallbackTangent))
    );
}

[[nodiscard]] inline SIMDMatrix LoadJointMatrix(const DeformableJointMatrix& matrix){
    SIMDMatrix result{};
    result.v[0] = LoadFloat(matrix.column0);
    result.v[1] = LoadFloat(matrix.column1);
    result.v[2] = LoadFloat(matrix.column2);
    result.v[3] = LoadFloat(matrix.column3);
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

