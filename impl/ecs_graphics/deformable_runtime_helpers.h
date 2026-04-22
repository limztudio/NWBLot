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

struct Vec3 : public Float4{
    constexpr Vec3()noexcept
        : Float4(0.0f, 0.0f, 0.0f)
    {}
    constexpr Vec3(const f32 _x, const f32 _y, const f32 _z)noexcept
        : Float4(_x, _y, _z)
    {}
};
static_assert(IsStandardLayout_V<Vec3>, "Vec3 must stay layout-stable");
static_assert(IsTriviallyCopyable_V<Vec3>, "Vec3 must stay cheap to pass by value");
static_assert(alignof(Vec3) >= alignof(Float4), "Vec3 must stay SIMD-aligned");
static_assert(sizeof(Vec3) == sizeof(Float4), "Vec3 must stay one aligned float3 wide");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVector LoadVec3(const Vec3& value){
    return LoadFloat(static_cast<const Float4&>(value));
}

[[nodiscard]] inline Vec3 StoreVec3(SIMDVector value){
    Vec3 result;
    StoreFloat(value, &result);
    return result;
}


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

[[nodiscard]] inline Vec3 ToVec3(const Float3U& value){
    return Vec3{ value.x, value.y, value.z };
}

[[nodiscard]] inline Vec3 ToVec3(const Float4& value){
    return Vec3{ value.x, value.y, value.z };
}

[[nodiscard]] inline Float3U ToFloat3(const Vec3& value){
    return Float3U(value.x, value.y, value.z);
}

[[nodiscard]] inline Float4 ToFloat4(const Vec3& value, const f32 w = 0.0f){
    return Float4(value.x, value.y, value.z, w);
}

inline void AccumulateScaled(Float3U& target, const Float3U& source, const f32 scalar){
    StoreFloat(VectorMultiplyAdd(LoadFloat(source), VectorReplicate(scalar), LoadFloat(target)), &target);
}

inline void AccumulateScaled(Float4U& target, const Float4U& source, const f32 scalar){
    StoreFloat(VectorMultiplyAdd(LoadFloat(source), VectorReplicate(scalar), LoadFloat(target)), &target);
}

[[nodiscard]] inline Vec3 Add(const Vec3& lhs, const Vec3& rhs){
    return StoreVec3(VectorAdd(LoadVec3(lhs), LoadVec3(rhs)));
}

[[nodiscard]] inline Vec3 Subtract(const Vec3& lhs, const Vec3& rhs){
    return StoreVec3(VectorSubtract(LoadVec3(lhs), LoadVec3(rhs)));
}

[[nodiscard]] inline Vec3 Scale(const Vec3& value, const f32 scalar){
    return StoreVec3(VectorScale(LoadVec3(value), scalar));
}

[[nodiscard]] inline f32 Dot(const Vec3& lhs, const Vec3& rhs){
    return VectorGetX(Vector3Dot(LoadVec3(lhs), LoadVec3(rhs)));
}

[[nodiscard]] inline Vec3 Cross(const Vec3& lhs, const Vec3& rhs){
    return StoreVec3(Vector3Cross(LoadVec3(lhs), LoadVec3(rhs)));
}

[[nodiscard]] inline f32 LengthSquared(const Vec3& value){
    return VectorGetX(Vector3LengthSq(LoadVec3(value)));
}

[[nodiscard]] inline f32 Length(const Vec3& value){
    return VectorGetX(Vector3Length(LoadVec3(value)));
}

[[nodiscard]] inline Vec3 Normalize(const Vec3& value, const Vec3& fallback){
    const SIMDVector valueVector = LoadVec3(value);
    const SIMDVector lengthSquaredVector = Vector3LengthSq(valueVector);
    const f32 lengthSquared = VectorGetX(lengthSquaredVector);
    if(lengthSquared <= s_FrameEpsilon)
        return fallback;
    if(!IsFinite(lengthSquared))
        return StoreVec3(Vector3Normalize(valueVector));

    return StoreVec3(VectorMultiply(valueVector, VectorReciprocalSqrt(lengthSquaredVector)));
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

[[nodiscard]] inline Vec3 RotateByQuaternion(const Vec3& value, const Float4& rotation){
    return StoreVec3(Vector3Rotate(LoadVec3(value), LoadFloat(rotation)));
}

[[nodiscard]] inline bool IsAffineJointMatrix(const DeformableJointMatrix& matrix){
    const SIMDVector affineW = VectorSet(matrix.column0.w, matrix.column1.w, matrix.column2.w, matrix.column3.w);
    return DeformableValidation::IsFiniteFloat4(matrix.column0)
        && DeformableValidation::IsFiniteFloat4(matrix.column1)
        && DeformableValidation::IsFiniteFloat4(matrix.column2)
        && DeformableValidation::IsFiniteFloat4(matrix.column3)
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

