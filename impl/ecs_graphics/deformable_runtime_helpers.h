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

[[nodiscard]] inline bool ResolveMorphWeightSum(
    const DeformableMorphWeightsComponent* weights,
    const Name& morphName,
    f32& outWeight)
{
    outWeight = 0.0f;
    if(!weights || !morphName)
        return true;

    for(const DeformableMorphWeight& weight : weights->weights){
        if(weight.morph != morphName)
            continue;
        if(!IsFinite(weight.weight))
            return false;

        outWeight += weight.weight;
        if(!IsFinite(outWeight))
            return false;
    }
    return true;
}

[[nodiscard]] inline SIMDVector Normalize(SIMDVector value, SIMDVector fallback){
    const SIMDVector lengthSquaredVector = Vector3LengthSq(value);
    const f32 lengthSquared = VectorGetX(lengthSquaredVector);
    if(lengthSquared <= s_FrameEpsilon)
        return fallback;
    if(!IsFinite(lengthSquared))
        return Vector3Normalize(value);

    return VectorMultiply(value, VectorReciprocalSqrt(lengthSquaredVector));
}

[[nodiscard]] inline SIMDVector FallbackTangent(SIMDVector normal){
    const SIMDVector axis = Abs(VectorGetZ(normal)) < 0.999f
        ? VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
        : VectorSet(0.0f, 1.0f, 0.0f, 0.0f)
    ;
    return Normalize(Vector3Cross(axis, normal), VectorSet(1.0f, 0.0f, 0.0f, 0.0f));
}

[[nodiscard]] inline bool IsAffineJointMatrix(const DeformableJointMatrix& matrix){
    const SIMDVector column0 = LoadFloat(matrix.column0);
    const SIMDVector column1 = LoadFloat(matrix.column1);
    const SIMDVector column2 = LoadFloat(matrix.column2);
    const SIMDVector column3 = LoadFloat(matrix.column3);
    const SIMDVector affineW = VectorSet(matrix.column0.w, matrix.column1.w, matrix.column2.w, matrix.column3.w);
    return DeformableValidation::FiniteVector(column0, 0xFu)
        && DeformableValidation::FiniteVector(column1, 0xFu)
        && DeformableValidation::FiniteVector(column2, 0xFu)
        && DeformableValidation::FiniteVector(column3, 0xFu)
        && Vector4NearEqual(affineW, s_SIMDIdentityR3, VectorReplicate(DeformableValidation::s_Epsilon))
    ;
}

[[nodiscard]] inline bool ValidateTriangleIndex(
    const DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    u32 (&outIndices)[3])
{
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

[[nodiscard]] inline bool ResolveEffectiveDisplacement(
    const DeformableDisplacement& sourceDisplacement,
    const DeformableDisplacementComponent* component,
    DeformableDisplacement& outDisplacement,
    DisplacementResolveFailure::Enum& outFailure)
{
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

[[nodiscard]] inline bool ResolveEffectiveDisplacement(
    const DeformableDisplacement& sourceDisplacement,
    const DeformableDisplacementComponent* component,
    DeformableDisplacement& outDisplacement)
{
    DisplacementResolveFailure::Enum failure = DisplacementResolveFailure::None;
    return ResolveEffectiveDisplacement(sourceDisplacement, component, outDisplacement, failure);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

