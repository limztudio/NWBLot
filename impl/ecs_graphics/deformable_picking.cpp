// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_picking.h"

#include "renderer_system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_picking{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_Epsilon = 0.000001f;
static constexpr f32 s_FrameEpsilon = 0.00000001f;
static constexpr f32 s_SkinWeightSumEpsilon = 0.001f;

struct Vec3{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
};

[[nodiscard]] bool ActiveWeight(const f32 value){
    return value > s_Epsilon || value < -s_Epsilon;
}

[[nodiscard]] f32 AbsF32(const f32 value){
    return value < 0.0f ? -value : value;
}

[[nodiscard]] f32 Clamp01(const f32 value){
    if(value < 0.0f)
        return 0.0f;
    if(value > 1.0f)
        return 1.0f;
    return value;
}

[[nodiscard]] bool IsFiniteVec3(const Float3Data& value){
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

[[nodiscard]] bool IsFiniteVec2(const Float2Data& value){
    return IsFinite(value.x) && IsFinite(value.y);
}

[[nodiscard]] bool IsFiniteVec4(const Float4Data& value){
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

[[nodiscard]] bool IsAffineJointMatrix(const DeformableJointMatrix& matrix){
    return IsFiniteVec4(matrix.column0)
        && IsFiniteVec4(matrix.column1)
        && IsFiniteVec4(matrix.column2)
        && IsFiniteVec4(matrix.column3)
        && !ActiveWeight(matrix.column0.w)
        && !ActiveWeight(matrix.column1.w)
        && !ActiveWeight(matrix.column2.w)
        && !ActiveWeight(matrix.column3.w - 1.0f)
    ;
}

[[nodiscard]] bool IsFiniteRay(const DeformablePickingRay& ray){
    return IsFiniteVec3(ray.origin)
        && IsFiniteVec3(ray.direction)
        && IsFinite(ray.minDistance)
        && IsFinite(ray.maxDistance)
        && ray.minDistance <= ray.maxDistance
    ;
}

[[nodiscard]] bool NearlyOne(const f32 value){
    const f32 difference = value > 1.0f ? value - 1.0f : 1.0f - value;
    return difference <= s_SkinWeightSumEpsilon;
}

[[nodiscard]] bool ValidBarycentric(const f32 (&bary)[3]){
    return IsFinite(bary[0])
        && IsFinite(bary[1])
        && IsFinite(bary[2])
        && bary[0] >= -s_Epsilon
        && bary[1] >= -s_Epsilon
        && bary[2] >= -s_Epsilon
        && NearlyOne(bary[0] + bary[1] + bary[2])
    ;
}

[[nodiscard]] bool ValidSourceSample(const SourceSample& sample, const u32 sourceTriangleCount){
    return sourceTriangleCount != 0u && sample.sourceTri < sourceTriangleCount && ValidBarycentric(sample.bary);
}

[[nodiscard]] bool ValidRestVertex(const DeformableVertexRest& vertex){
    return IsFiniteVec3(vertex.position)
        && IsFiniteVec3(vertex.normal)
        && IsFiniteVec4(vertex.tangent)
        && IsFiniteVec2(vertex.uv0)
        && IsFiniteVec4(vertex.color0)
    ;
}

void AssignCurrentTriangleSample(const u32 triangle, const f32 (&bary)[3], SourceSample& outSample){
    outSample.sourceTri = triangle;
    outSample.bary[0] = bary[0];
    outSample.bary[1] = bary[1];
    outSample.bary[2] = bary[2];
}

[[nodiscard]] Vec3 ToVec3(const Float3Data& value){
    return Vec3{ value.x, value.y, value.z };
}

[[nodiscard]] Float3Data ToFloat3(const Vec3& value){
    return Float3Data(value.x, value.y, value.z);
}

[[nodiscard]] Vec3 Add(const Vec3& lhs, const Vec3& rhs){
    return Vec3{ lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
}

[[nodiscard]] Vec3 Subtract(const Vec3& lhs, const Vec3& rhs){
    return Vec3{ lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
}

[[nodiscard]] Vec3 Scale(const Vec3& value, const f32 scalar){
    return Vec3{ value.x * scalar, value.y * scalar, value.z * scalar };
}

[[nodiscard]] f32 Dot(const Vec3& lhs, const Vec3& rhs){
    return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
}

[[nodiscard]] Vec3 Cross(const Vec3& lhs, const Vec3& rhs){
    return Vec3{
        (lhs.y * rhs.z) - (lhs.z * rhs.y),
        (lhs.z * rhs.x) - (lhs.x * rhs.z),
        (lhs.x * rhs.y) - (lhs.y * rhs.x),
    };
}

[[nodiscard]] f32 LengthSquared(const Vec3& value){
    return Dot(value, value);
}

[[nodiscard]] Vec3 Normalize(const Vec3& value, const Vec3& fallback){
    const f32 lengthSquared = LengthSquared(value);
    if(lengthSquared <= s_FrameEpsilon)
        return fallback;

    return Scale(value, 1.0f / Sqrt(lengthSquared));
}

[[nodiscard]] Vec3 FallbackTangent(const Vec3& normal){
    const Vec3 axis = AbsF32(normal.z) < 0.999f
        ? Vec3{ 0.0f, 0.0f, 1.0f }
        : Vec3{ 0.0f, 1.0f, 0.0f }
    ;
    return Normalize(Cross(axis, normal), Vec3{ 1.0f, 0.0f, 0.0f });
}

void OrthonormalizeFrame(
    Vec3& normal,
    Float4Data& tangent,
    const Vec3& fallbackNormal,
    const Float4Data& fallbackTangent)
{
    normal = Normalize(normal, Normalize(fallbackNormal, Vec3{ 0.0f, 0.0f, 1.0f }));

    Vec3 tangentVector{ tangent.x, tangent.y, tangent.z };
    Vec3 projectedTangent = Subtract(tangentVector, Scale(normal, Dot(tangentVector, normal)));
    if(LengthSquared(projectedTangent) <= s_FrameEpsilon){
        const Vec3 fallbackTangentVector{ fallbackTangent.x, fallbackTangent.y, fallbackTangent.z };
        projectedTangent = Subtract(fallbackTangentVector, Scale(normal, Dot(fallbackTangentVector, normal)));
    }
    if(LengthSquared(projectedTangent) <= s_FrameEpsilon)
        projectedTangent = FallbackTangent(normal);

    projectedTangent = Normalize(projectedTangent, FallbackTangent(normal));
    tangent.x = projectedTangent.x;
    tangent.y = projectedTangent.y;
    tangent.z = projectedTangent.z;
    tangent.w = AbsF32(tangent.w) > s_Epsilon
        ? (tangent.w < 0.0f ? -1.0f : 1.0f)
        : (fallbackTangent.w < 0.0f ? -1.0f : 1.0f)
    ;
}

[[nodiscard]] bool ResolveMorphWeight(
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

[[nodiscard]] bool ApplyMorphs(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableMorphWeightsComponent* weights,
    Vector<DeformableVertexRest>& vertices)
{
    for(const DeformableMorph& morph : instance.morphs){
        f32 weight = 0.0f;
        if(!ResolveMorphWeight(weights, morph.name, weight))
            return false;
        if(!ActiveWeight(weight))
            continue;
        if(morph.deltas.empty())
            return false;

        for(const DeformableMorphDelta& delta : morph.deltas){
            if(delta.vertexId >= vertices.size()
                || !IsFiniteVec3(delta.deltaPosition)
                || !IsFiniteVec3(delta.deltaNormal)
                || !IsFiniteVec4(delta.deltaTangent)
            )
                return false;

            DeformableVertexRest& vertex = vertices[delta.vertexId];
            vertex.position.x += weight * delta.deltaPosition.x;
            vertex.position.y += weight * delta.deltaPosition.y;
            vertex.position.z += weight * delta.deltaPosition.z;
            vertex.normal.x += weight * delta.deltaNormal.x;
            vertex.normal.y += weight * delta.deltaNormal.y;
            vertex.normal.z += weight * delta.deltaNormal.z;
            vertex.tangent.x += weight * delta.deltaTangent.x;
            vertex.tangent.y += weight * delta.deltaTangent.y;
            vertex.tangent.z += weight * delta.deltaTangent.z;
            vertex.tangent.w += weight * delta.deltaTangent.w;
        }
    }
    return true;
}

[[nodiscard]] Vec3 TransformJointPosition(const DeformableJointMatrix& matrix, const Vec3& position){
    return Vec3{
        (matrix.column0.x * position.x) + (matrix.column1.x * position.y) + (matrix.column2.x * position.z) + matrix.column3.x,
        (matrix.column0.y * position.x) + (matrix.column1.y * position.y) + (matrix.column2.y * position.z) + matrix.column3.y,
        (matrix.column0.z * position.x) + (matrix.column1.z * position.y) + (matrix.column2.z * position.z) + matrix.column3.z,
    };
}

[[nodiscard]] Vec3 TransformJointDirection(const DeformableJointMatrix& matrix, const Vec3& direction){
    return Vec3{
        (matrix.column0.x * direction.x) + (matrix.column1.x * direction.y) + (matrix.column2.x * direction.z),
        (matrix.column0.y * direction.x) + (matrix.column1.y * direction.y) + (matrix.column2.y * direction.z),
        (matrix.column0.z * direction.x) + (matrix.column1.z * direction.y) + (matrix.column2.z * direction.z),
    };
}

[[nodiscard]] bool ValidateSkinInfluence(const SkinInfluence4& skin){
    f32 weightSum = 0.0f;
    for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
        const f32 weight = skin.weight[influenceIndex];
        if(!IsFinite(weight) || weight < 0.0f)
            return false;

        weightSum += weight;
        if(!IsFinite(weightSum))
            return false;
    }

    return NearlyOne(weightSum);
}

[[nodiscard]] bool ValidateJointPalette(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableJointPaletteComponent* jointPalette)
{
    if(instance.skin.empty() || !jointPalette || jointPalette->joints.empty())
        return true;

    for(const DeformableJointMatrix& joint : jointPalette->joints){
        if(!IsAffineJointMatrix(joint))
            return false;
    }
    return true;
}

[[nodiscard]] bool ApplySkin(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableJointPaletteComponent* jointPalette,
    const u32 vertexId,
    DeformableVertexRest& vertex)
{
    if(!jointPalette || jointPalette->joints.empty())
        return true;
    if(instance.skin.empty())
        return true;
    if(instance.skin.size() != instance.restVertices.size() || vertexId >= instance.skin.size())
        return false;

    const SkinInfluence4& skin = instance.skin[vertexId];
    if(!ValidateSkinInfluence(skin))
        return false;

    Vec3 skinnedPosition;
    Vec3 skinnedNormal;
    Vec3 skinnedTangent;
    f32 totalWeight = 0.0f;

    for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
        const f32 weight = skin.weight[influenceIndex];
        const u32 joint = static_cast<u32>(skin.joint[influenceIndex]);
        if(!ActiveWeight(weight))
            continue;
        if(joint >= jointPalette->joints.size() || !IsAffineJointMatrix(jointPalette->joints[joint]))
            return false;

        const DeformableJointMatrix& matrix = jointPalette->joints[joint];
        skinnedPosition = Add(skinnedPosition, Scale(TransformJointPosition(matrix, ToVec3(vertex.position)), weight));
        skinnedNormal = Add(skinnedNormal, Scale(TransformJointDirection(matrix, ToVec3(vertex.normal)), weight));
        const Vec3 tangentVector{ vertex.tangent.x, vertex.tangent.y, vertex.tangent.z };
        skinnedTangent = Add(skinnedTangent, Scale(TransformJointDirection(matrix, tangentVector), weight));
        totalWeight += weight;
    }

    if(!ActiveWeight(totalWeight))
        return true;

    vertex.position = ToFloat3(skinnedPosition);
    vertex.normal = ToFloat3(skinnedNormal);
    vertex.tangent.x = skinnedTangent.x;
    vertex.tangent.y = skinnedTangent.y;
    vertex.tangent.z = skinnedTangent.z;
    return true;
}

[[nodiscard]] bool ResolveDisplacement(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableDisplacementComponent* component,
    DeformableDisplacement& outDisplacement)
{
    outDisplacement = instance.displacement;
    if(!ValidDeformableDisplacementDescriptor(outDisplacement))
        return false;
    if(outDisplacement.mode == DeformableDisplacementMode::None)
        return true;
    if(component && !component->enabled)
        outDisplacement = DeformableDisplacement{};

    if(outDisplacement.mode == DeformableDisplacementMode::None)
        return true;

    const f32 scale = component ? component->amplitudeScale : 1.0f;
    if(!IsFinite(scale))
        return false;

    outDisplacement.amplitude *= scale;
    if(!IsFinite(outDisplacement.amplitude))
        return false;
    if(!ActiveWeight(outDisplacement.amplitude))
        outDisplacement = DeformableDisplacement{};
    return true;
}

void ApplyDisplacement(const DeformableDisplacement& displacement, DeformableVertexRest& vertex){
    if(displacement.mode != DeformableDisplacementMode::ScalarUvRamp)
        return;

    const f32 offset = Clamp01(vertex.uv0.x) * displacement.amplitude;
    if(!ActiveWeight(offset))
        return;

    vertex.position.x += vertex.normal.x * offset;
    vertex.position.y += vertex.normal.y * offset;
    vertex.position.z += vertex.normal.z * offset;
}

[[nodiscard]] Vec3 RotateByQuaternion(const Vec3& value, const AlignedFloat4Data& rotation){
    const Vec3 q{ rotation.x, rotation.y, rotation.z };
    const Vec3 twiceCross = Scale(Cross(q, value), 2.0f);
    return Add(Add(value, Scale(twiceCross, rotation.w)), Cross(q, twiceCross));
}

void ApplyTransform(const Core::ECS::TransformComponent* transform, DeformableVertexRest& vertex){
    if(!transform)
        return;

    Vec3 position = ToVec3(vertex.position);
    position.x *= transform->scale.x;
    position.y *= transform->scale.y;
    position.z *= transform->scale.z;
    position = RotateByQuaternion(position, transform->rotation);
    position.x += transform->position.x;
    position.y += transform->position.y;
    position.z += transform->position.z;
    vertex.position = ToFloat3(position);

    Vec3 normal = RotateByQuaternion(ToVec3(vertex.normal), transform->rotation);
    normal = Normalize(normal, Vec3{ 0.0f, 0.0f, 1.0f });
    vertex.normal = ToFloat3(normal);

    Vec3 tangent = RotateByQuaternion(Vec3{ vertex.tangent.x, vertex.tangent.y, vertex.tangent.z }, transform->rotation);
    tangent = Normalize(tangent, FallbackTangent(normal));
    vertex.tangent.x = tangent.x;
    vertex.tangent.y = tangent.y;
    vertex.tangent.z = tangent.z;
}

[[nodiscard]] bool ValidateTriangleIndex(
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

[[nodiscard]] bool IntersectTriangle(
    const Vec3& origin,
    const Vec3& direction,
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    f32& outDistance,
    f32 (&outBary)[3])
{
    const Vec3 edge0 = Subtract(b, a);
    const Vec3 edge1 = Subtract(c, a);
    const Vec3 p = Cross(direction, edge1);
    const f32 determinant = Dot(edge0, p);
    if(AbsF32(determinant) <= s_Epsilon)
        return false;

    const f32 invDeterminant = 1.0f / determinant;
    const Vec3 t = Subtract(origin, a);
    const f32 u = Dot(t, p) * invDeterminant;
    if(u < -s_Epsilon || u > 1.0f + s_Epsilon)
        return false;

    const Vec3 q = Cross(t, edge0);
    const f32 v = Dot(direction, q) * invDeterminant;
    if(v < -s_Epsilon || (u + v) > 1.0f + s_Epsilon)
        return false;

    const f32 distance = Dot(edge1, q) * invDeterminant;
    if(!IsFinite(distance))
        return false;

    outDistance = distance;
    outBary[0] = 1.0f - u - v;
    outBary[1] = u;
    outBary[2] = v;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};

using __hidden_deformable_picking::Vec3;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildDeformablePickingVertices(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    Vector<DeformableVertexRest>& outVertices)
{
    outVertices.clear();
    if(instance.restVertices.empty() || instance.indices.empty() || (instance.indices.size() % 3u) != 0u)
        return false;
    if(instance.restVertices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || instance.indices.size() > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;
    for(const DeformableVertexRest& vertex : instance.restVertices){
        if(!__hidden_deformable_picking::ValidRestVertex(vertex))
            return false;
    }
    for(const u32 index : instance.indices){
        if(index >= instance.restVertices.size())
            return false;
    }

    DeformableDisplacement displacement;
    if(!__hidden_deformable_picking::ResolveDisplacement(instance, inputs.displacement, displacement))
        return false;
    if(!__hidden_deformable_picking::ValidateJointPalette(instance, inputs.jointPalette))
        return false;

    outVertices = instance.restVertices;
    if(!__hidden_deformable_picking::ApplyMorphs(instance, inputs.morphWeights, outVertices))
        return false;

    for(usize vertexIndex = 0; vertexIndex < outVertices.size(); ++vertexIndex){
        DeformableVertexRest& vertex = outVertices[vertexIndex];
        const Vec3 restNormal = __hidden_deformable_picking::ToVec3(vertex.normal);
        const Float4Data restTangent = vertex.tangent;

        Vec3 normal = __hidden_deformable_picking::ToVec3(vertex.normal);
        __hidden_deformable_picking::OrthonormalizeFrame(normal, vertex.tangent, restNormal, restTangent);
        vertex.normal = __hidden_deformable_picking::ToFloat3(normal);

        const Vec3 preSkinNormal = normal;
        const Float4Data preSkinTangent = vertex.tangent;
        if(!__hidden_deformable_picking::ApplySkin(
            instance,
            inputs.jointPalette,
            static_cast<u32>(vertexIndex),
            vertex
        ))
            return false;
        normal = __hidden_deformable_picking::ToVec3(vertex.normal);
        __hidden_deformable_picking::OrthonormalizeFrame(normal, vertex.tangent, preSkinNormal, preSkinTangent);
        vertex.normal = __hidden_deformable_picking::ToFloat3(normal);

        __hidden_deformable_picking::ApplyDisplacement(displacement, vertex);
        __hidden_deformable_picking::ApplyTransform(inputs.transform, vertex);
    }

    return true;
}

bool ResolveDeformableRestSurfaceSample(
    const DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    const f32 (&bary)[3],
    SourceSample& outSample)
{
    outSample = SourceSample{};
    if(!__hidden_deformable_picking::ValidBarycentric(bary))
        return false;
    if(instance.indices.empty() || (instance.indices.size() % 3u) != 0u)
        return false;

    u32 vertexIndices[3] = {};
    if(!__hidden_deformable_picking::ValidateTriangleIndex(instance, triangle, vertexIndices))
        return false;

    const usize triangleCount = instance.indices.size() / 3u;
    if(instance.sourceSamples.empty()){
        if(triangle >= triangleCount)
            return false;

        __hidden_deformable_picking::AssignCurrentTriangleSample(triangle, bary, outSample);
        return true;
    }
    if(instance.sourceSamples.size() != instance.restVertices.size())
        return false;
    if(instance.sourceTriangleCount == 0u)
        return false;

    const SourceSample& sample0 = instance.sourceSamples[vertexIndices[0]];
    const SourceSample& sample1 = instance.sourceSamples[vertexIndices[1]];
    const SourceSample& sample2 = instance.sourceSamples[vertexIndices[2]];
    if(!__hidden_deformable_picking::ValidSourceSample(sample0, instance.sourceTriangleCount)
        || !__hidden_deformable_picking::ValidSourceSample(sample1, instance.sourceTriangleCount)
        || !__hidden_deformable_picking::ValidSourceSample(sample2, instance.sourceTriangleCount)
    )
        return false;
    if(sample0.sourceTri != sample1.sourceTri || sample0.sourceTri != sample2.sourceTri){
        if(triangle >= triangleCount)
            return false;

        __hidden_deformable_picking::AssignCurrentTriangleSample(triangle, bary, outSample);
        return true;
    }

    outSample.sourceTri = sample0.sourceTri;
    for(u32 i = 0; i < 3u; ++i){
        outSample.bary[i] =
            (bary[0] * sample0.bary[i])
            + (bary[1] * sample1.bary[i])
            + (bary[2] * sample2.bary[i])
        ;
    }
    return true;
}

bool RaycastDeformableRuntimeMesh(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    const DeformablePickingRay& ray,
    DeformablePosedHit& outHit)
{
    outHit = DeformablePosedHit{};
    if(!instance.entity.valid() || !instance.handle.valid() || !__hidden_deformable_picking::IsFiniteRay(ray))
        return false;

    const Vec3 rayOrigin = __hidden_deformable_picking::ToVec3(ray.origin);
    const Vec3 rayDirection = __hidden_deformable_picking::Normalize(
        __hidden_deformable_picking::ToVec3(ray.direction),
        Vec3{}
    );
    if(__hidden_deformable_picking::LengthSquared(rayDirection) <= __hidden_deformable_picking::s_FrameEpsilon)
        return false;

    Vector<DeformableVertexRest> posedVertices;
    if(!BuildDeformablePickingVertices(instance, inputs, posedVertices))
        return false;

    const usize triangleCount = instance.indices.size() / 3u;
    bool foundHit = false;
    f32 closestDistance = ray.maxDistance;
    DeformablePosedHit closestHit;
    for(usize triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex){
        u32 vertexIndices[3] = {};
        if(!__hidden_deformable_picking::ValidateTriangleIndex(instance, static_cast<u32>(triangleIndex), vertexIndices))
            return false;

        const Vec3 a = __hidden_deformable_picking::ToVec3(posedVertices[vertexIndices[0]].position);
        const Vec3 b = __hidden_deformable_picking::ToVec3(posedVertices[vertexIndices[1]].position);
        const Vec3 c = __hidden_deformable_picking::ToVec3(posedVertices[vertexIndices[2]].position);

        f32 distance = 0.0f;
        f32 bary[3] = {};
        if(!__hidden_deformable_picking::IntersectTriangle(rayOrigin, rayDirection, a, b, c, distance, bary))
            continue;
        if(distance < ray.minDistance || distance > closestDistance)
            continue;

        SourceSample restSample;
        if(!ResolveDeformableRestSurfaceSample(instance, static_cast<u32>(triangleIndex), bary, restSample))
            continue;

        const Vec3 edge0 = __hidden_deformable_picking::Subtract(b, a);
        const Vec3 edge1 = __hidden_deformable_picking::Subtract(c, a);
        const Vec3 normal = __hidden_deformable_picking::Normalize(
            __hidden_deformable_picking::Cross(edge0, edge1),
            Vec3{ 0.0f, 0.0f, 1.0f }
        );
        const Vec3 position = __hidden_deformable_picking::Add(
            rayOrigin,
            __hidden_deformable_picking::Scale(rayDirection, distance)
        );

        closestDistance = distance;
        closestHit.entity = instance.entity;
        closestHit.runtimeMesh = instance.handle;
        closestHit.editRevision = instance.editRevision;
        closestHit.triangle = static_cast<u32>(triangleIndex);
        closestHit.bary[0] = bary[0];
        closestHit.bary[1] = bary[1];
        closestHit.bary[2] = bary[2];
        closestHit.distance = distance;
        closestHit.position = __hidden_deformable_picking::ToFloat3(position);
        closestHit.normal = __hidden_deformable_picking::ToFloat3(normal);
        closestHit.restSample = restSample;
        foundHit = true;
    }

    if(!foundHit)
        return false;

    outHit = closestHit;
    return true;
}

bool RaycastVisibleDeformableRenderers(
    Core::ECS::World& world,
    const RendererSystem& rendererSystem,
    const DeformablePickingRay& ray,
    DeformablePosedHit& outHit)
{
    outHit = DeformablePosedHit{};
    bool foundHit = false;
    f32 closestDistance = ray.maxDistance;

    world.view<DeformableRendererComponent>().each(
        [&](Core::ECS::EntityID entity, DeformableRendererComponent& renderer){
            if(!renderer.visible || !renderer.runtimeMesh.valid())
                return;

            const DeformableRuntimeMeshInstance* instance =
                rendererSystem.findDeformableRuntimeMesh(renderer.runtimeMesh)
            ;
            if(!instance)
                return;

            DeformablePickingInputs inputs;
            inputs.morphWeights = world.tryGetComponent<DeformableMorphWeightsComponent>(entity);
            inputs.jointPalette = world.tryGetComponent<DeformableJointPaletteComponent>(entity);
            inputs.displacement = world.tryGetComponent<DeformableDisplacementComponent>(entity);
            inputs.transform = world.tryGetComponent<Core::ECS::TransformComponent>(entity);

            DeformablePosedHit hit;
            if(!RaycastDeformableRuntimeMesh(*instance, inputs, ray, hit))
                return;
            if(!foundHit || hit.distance < closestDistance){
                closestDistance = hit.distance;
                outHit = hit;
                foundHit = true;
            }
        }
    );

    return foundHit;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
