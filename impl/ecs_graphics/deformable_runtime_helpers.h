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

struct Vec3{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ActiveWeight(const f32 value){
    return DeformableValidation::ActiveWeight(value);
}

[[nodiscard]] inline bool ActiveLength(const f32 value){
    return value > s_Epsilon;
}

[[nodiscard]] inline bool ActiveDisplacement(const DeformableDisplacement& displacement){
    return displacement.mode != DeformableDisplacementMode::None && ActiveWeight(displacement.amplitude);
}

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

[[nodiscard]] inline Vec3 ToVec3(const Float3Data& value){
    return Vec3{ value.x, value.y, value.z };
}

[[nodiscard]] inline Float3Data ToFloat3(const Vec3& value){
    return Float3Data(value.x, value.y, value.z);
}

[[nodiscard]] inline Vec3 Add(const Vec3& lhs, const Vec3& rhs){
    return Vec3{ lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
}

[[nodiscard]] inline Vec3 Subtract(const Vec3& lhs, const Vec3& rhs){
    return Vec3{ lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
}

[[nodiscard]] inline Vec3 Scale(const Vec3& value, const f32 scalar){
    return Vec3{ value.x * scalar, value.y * scalar, value.z * scalar };
}

[[nodiscard]] inline f32 Dot(const Vec3& lhs, const Vec3& rhs){
    return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
}

[[nodiscard]] inline Vec3 Cross(const Vec3& lhs, const Vec3& rhs){
    return Vec3{
        (lhs.y * rhs.z) - (lhs.z * rhs.y),
        (lhs.z * rhs.x) - (lhs.x * rhs.z),
        (lhs.x * rhs.y) - (lhs.y * rhs.x),
    };
}

[[nodiscard]] inline f32 LengthSquared(const Vec3& value){
    return Dot(value, value);
}

[[nodiscard]] inline f32 Length(const Vec3& value){
    return Sqrt(LengthSquared(value));
}

[[nodiscard]] inline Vec3 Normalize(const Vec3& value, const Vec3& fallback){
    const f32 lengthSquared = LengthSquared(value);
    if(lengthSquared <= s_FrameEpsilon)
        return fallback;

    return Scale(value, 1.0f / Sqrt(lengthSquared));
}

[[nodiscard]] inline Vec3 FallbackTangent(const Vec3& normal){
    const Vec3 axis = DeformableValidation::AbsF32(normal.z) < 0.999f
        ? Vec3{ 0.0f, 0.0f, 1.0f }
        : Vec3{ 0.0f, 1.0f, 0.0f }
    ;
    return Normalize(Cross(axis, normal), Vec3{ 1.0f, 0.0f, 0.0f });
}

[[nodiscard]] inline f32 TangentHandedness(const f32 value){
    return value < 0.0f ? -1.0f : 1.0f;
}

[[nodiscard]] inline Vec3 RotateByQuaternion(const Vec3& value, const AlignedFloat4Data& rotation){
    const Vec3 q{ rotation.x, rotation.y, rotation.z };
    const Vec3 twiceCross = Scale(Cross(q, value), 2.0f);
    return Add(Add(value, Scale(twiceCross, rotation.w)), Cross(q, twiceCross));
}

[[nodiscard]] inline bool IsAffineJointMatrix(const DeformableJointMatrix& matrix){
    return DeformableValidation::IsFiniteFloat4(matrix.column0)
        && DeformableValidation::IsFiniteFloat4(matrix.column1)
        && DeformableValidation::IsFiniteFloat4(matrix.column2)
        && DeformableValidation::IsFiniteFloat4(matrix.column3)
        && !ActiveWeight(matrix.column0.w)
        && !ActiveWeight(matrix.column1.w)
        && !ActiveWeight(matrix.column2.w)
        && !ActiveWeight(matrix.column3.w - 1.0f)
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
    if(!ActiveWeight(outDisplacement.amplitude))
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

