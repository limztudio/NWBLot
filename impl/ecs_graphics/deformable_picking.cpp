// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_picking.h"

#include "deformable_runtime_helpers.h"
#include "renderer_system.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_picking{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace DeformableRuntime;

[[nodiscard]] bool IsFiniteRay(const DeformablePickingRay& ray){
    return DeformableValidation::IsFiniteFloat3(ray.origin)
        && DeformableValidation::IsFiniteFloat3(ray.direction)
        && IsFinite(ray.minDistance)
        && IsFinite(ray.maxDistance)
        && ray.minDistance >= 0.0f
        && ray.minDistance <= ray.maxDistance
    ;
}

[[nodiscard]] bool AssignCurrentTriangleSample(const u32 triangle, const f32 (&bary)[3], SourceSample& outSample){
    outSample.sourceTri = triangle;
    return DeformableValidation::NormalizeSourceBarycentric(bary, outSample.bary);
}

[[nodiscard]] bool AssignStableCurrentTriangleSample(
    const DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    const usize triangleCount,
    const f32 (&bary)[3],
    SourceSample& outSample)
{
    if(instance.editRevision != 0u)
        return false;
    if(instance.sourceTriangleCount == 0u)
        return false;
    if(triangle >= triangleCount)
        return false;
    if(triangleCount != static_cast<usize>(instance.sourceTriangleCount))
        return false;
    if(triangle >= instance.sourceTriangleCount)
        return false;

    return AssignCurrentTriangleSample(triangle, bary, outSample);
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
    tangent.w = TangentHandedness(
        DeformableValidation::AbsF32(tangent.w) > s_Epsilon ? tangent.w : fallbackTangent.w
    );
}

template<typename VertexVector>
[[nodiscard]] bool ApplyMorphs(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableMorphWeightsComponent* weights,
    VertexVector& vertices)
{
    for(const DeformableMorph& morph : instance.morphs){
        f32 weight = 0.0f;
        if(!ResolveMorphWeightSum(weights, morph.name, weight))
            return false;
        if(!ActiveWeight(weight))
            continue;
        if(morph.deltas.empty())
            return false;

        for(const DeformableMorphDelta& delta : morph.deltas){
            if(!DeformableValidation::ValidMorphDelta(delta, vertices.size()))
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
    if(!DeformableValidation::ValidSkinInfluence(skin))
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

void ApplyDisplacement(const DeformableDisplacement& displacement, DeformableVertexRest& vertex){
    if(displacement.mode != DeformableDisplacementMode::ScalarUvRamp)
        return;

    const f32 offset = DeformableValidation::Clamp01(vertex.uv0.x) * displacement.amplitude;
    if(!ActiveWeight(offset))
        return;

    vertex.position.x += vertex.normal.x * offset;
    vertex.position.y += vertex.normal.y * offset;
    vertex.position.z += vertex.normal.z * offset;
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
    if(DeformableValidation::AbsF32(determinant) <= s_Epsilon)
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


template<typename VertexVector>
[[nodiscard]] bool BuildPickingVertices(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    VertexVector& outVertices)
{
    outVertices.clear();
    if(!DeformableValidation::ValidRuntimePayloadArrays(
            instance.restVertices,
            instance.indices,
            instance.sourceTriangleCount,
            instance.skin,
            instance.sourceSamples,
            instance.morphs
        )
    )
        return false;

    DeformableDisplacement displacement;
    if(!DeformableRuntime::ResolveEffectiveDisplacement(instance.displacement, inputs.displacement, displacement))
        return false;
    if(!__hidden_deformable_picking::ValidateJointPalette(instance, inputs.jointPalette))
        return false;

    outVertices.reserve(instance.restVertices.size());
    outVertices.assign(instance.restVertices.begin(), instance.restVertices.end());

    if(!__hidden_deformable_picking::ApplyMorphs(instance, inputs.morphWeights, outVertices))
        return false;

    for(usize vertexIndex = 0; vertexIndex < outVertices.size(); ++vertexIndex){
        DeformableVertexRest& vertex = outVertices[vertexIndex];
        const Vec3 restNormal = DeformableRuntime::ToVec3(vertex.normal);
        const Float4Data restTangent = vertex.tangent;

        Vec3 normal = DeformableRuntime::ToVec3(vertex.normal);
        __hidden_deformable_picking::OrthonormalizeFrame(normal, vertex.tangent, restNormal, restTangent);
        vertex.normal = DeformableRuntime::ToFloat3(normal);

        const Vec3 preSkinNormal = normal;
        const Float4Data preSkinTangent = vertex.tangent;
        if(!__hidden_deformable_picking::ApplySkin(
            instance,
            inputs.jointPalette,
            static_cast<u32>(vertexIndex),
            vertex
        ))
            return false;
        normal = DeformableRuntime::ToVec3(vertex.normal);
        __hidden_deformable_picking::OrthonormalizeFrame(normal, vertex.tangent, preSkinNormal, preSkinTangent);
        vertex.normal = DeformableRuntime::ToFloat3(normal);

        __hidden_deformable_picking::ApplyDisplacement(displacement, vertex);
        __hidden_deformable_picking::ApplyTransform(inputs.transform, vertex);
        if(!DeformableValidation::ValidRestVertexFrame(vertex))
            return false;
    }

    return true;
}

template<typename VertexVector>
[[nodiscard]] bool BuildPickingVerticesIfReady(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    VertexVector& outVertices)
{
    outVertices.clear();
    if((instance.dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u)
        return false;

    return BuildPickingVertices(instance, inputs, outVertices);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildDeformablePickingVertices(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    Vector<DeformableVertexRest>& outVertices)
{
    return __hidden_deformable_picking::BuildPickingVerticesIfReady(instance, inputs, outVertices);
}

bool BuildDeformablePickingVertices(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    Vector<DeformableVertexRest, Core::Alloc::ScratchAllocator<DeformableVertexRest>>& outVertices)
{
    return __hidden_deformable_picking::BuildPickingVerticesIfReady(instance, inputs, outVertices);
}

bool ResolveDeformableRestSurfaceSample(
    const DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    const f32 (&bary)[3],
    SourceSample& outSample)
{
    outSample = SourceSample{};
    if(!DeformableValidation::ValidLooseBarycentric(bary))
        return false;
    if(instance.indices.empty() || (instance.indices.size() % 3u) != 0u)
        return false;

    u32 vertexIndices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, triangle, vertexIndices))
        return false;

    const usize triangleCount = instance.indices.size() / 3u;
    if(instance.sourceSamples.empty()){
        return __hidden_deformable_picking::AssignStableCurrentTriangleSample(
            instance,
            triangle,
            triangleCount,
            bary,
            outSample
        );
    }
    if(instance.sourceSamples.size() != instance.restVertices.size())
        return false;
    if(instance.sourceTriangleCount == 0u)
        return false;

    const SourceSample& sample0 = instance.sourceSamples[vertexIndices[0]];
    const SourceSample& sample1 = instance.sourceSamples[vertexIndices[1]];
    const SourceSample& sample2 = instance.sourceSamples[vertexIndices[2]];
    if(!DeformableValidation::ValidSourceSample(sample0, instance.sourceTriangleCount)
        || !DeformableValidation::ValidSourceSample(sample1, instance.sourceTriangleCount)
        || !DeformableValidation::ValidSourceSample(sample2, instance.sourceTriangleCount)
    )
        return false;
    if(sample0.sourceTri != sample1.sourceTri || sample0.sourceTri != sample2.sourceTri){
        return __hidden_deformable_picking::AssignStableCurrentTriangleSample(
            instance,
            triangle,
            triangleCount,
            bary,
            outSample
        );
    }

    outSample.sourceTri = sample0.sourceTri;
    f32 rawBary[3] = {};
    for(u32 i = 0; i < 3u; ++i){
        rawBary[i] =
            (bary[0] * sample0.bary[i])
            + (bary[1] * sample1.bary[i])
            + (bary[2] * sample2.bary[i])
        ;
    }
    return DeformableValidation::NormalizeSourceBarycentric(rawBary, outSample.bary);
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
    if((instance.dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u)
        return false;

    using DeformableRuntime::Vec3;
    const Vec3 rayOrigin = DeformableRuntime::ToVec3(ray.origin);
    const Vec3 rayDirection = DeformableRuntime::Normalize(
        DeformableRuntime::ToVec3(ray.direction),
        Vec3{}
    );
    if(DeformableRuntime::LengthSquared(rayDirection) <= DeformableRuntime::s_FrameEpsilon)
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<DeformableVertexRest, Core::Alloc::ScratchAllocator<DeformableVertexRest>> posedVertices{
        Core::Alloc::ScratchAllocator<DeformableVertexRest>(scratchArena)
    };
    if(!__hidden_deformable_picking::BuildPickingVertices(instance, inputs, posedVertices))
        return false;

    const usize triangleCount = instance.indices.size() / 3u;
    bool foundHit = false;
    f32 closestDistance = ray.maxDistance;
    DeformablePosedHit closestHit;
    for(usize triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex){
        u32 vertexIndices[3] = {};
        if(!DeformableRuntime::ValidateTriangleIndex(instance, static_cast<u32>(triangleIndex), vertexIndices))
            return false;

        const Vec3 a = DeformableRuntime::ToVec3(posedVertices[vertexIndices[0]].position);
        const Vec3 b = DeformableRuntime::ToVec3(posedVertices[vertexIndices[1]].position);
        const Vec3 c = DeformableRuntime::ToVec3(posedVertices[vertexIndices[2]].position);

        f32 distance = 0.0f;
        f32 bary[3] = {};
        if(!__hidden_deformable_picking::IntersectTriangle(rayOrigin, rayDirection, a, b, c, distance, bary))
            continue;
        if(distance < ray.minDistance || distance > closestDistance)
            continue;

        f32 hitBary[3] = {};
        if(!DeformableValidation::NormalizeSourceBarycentric(bary, hitBary))
            continue;

        SourceSample restSample{};
        if(!ResolveDeformableRestSurfaceSample(instance, static_cast<u32>(triangleIndex), hitBary, restSample))
            continue;

        const Vec3 edge0 = DeformableRuntime::Subtract(b, a);
        const Vec3 edge1 = DeformableRuntime::Subtract(c, a);
        const Vec3 normal = DeformableRuntime::Normalize(
            DeformableRuntime::Cross(edge0, edge1),
            Vec3{ 0.0f, 0.0f, 1.0f }
        );
        const Vec3 position = DeformableRuntime::Add(
            rayOrigin,
            DeformableRuntime::Scale(rayDirection, distance)
        );

        closestDistance = distance;
        closestHit.entity = instance.entity;
        closestHit.runtimeMesh = instance.handle;
        closestHit.editRevision = instance.editRevision;
        closestHit.triangle = static_cast<u32>(triangleIndex);
        closestHit.bary[0] = hitBary[0];
        closestHit.bary[1] = hitBary[1];
        closestHit.bary[2] = hitBary[2];
        closestHit.distance = distance;
        closestHit.position = DeformableRuntime::ToFloat3(position);
        closestHit.normal = DeformableRuntime::ToFloat3(normal);
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
            if(instance->entity != entity)
                return;
            if((instance->dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u)
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

