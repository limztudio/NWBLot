// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_picking.h"

#include "deformable_runtime_helpers.h"
#include "renderer_system.h"

#include <core/alloc/scratch.h>
#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_picking{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace DeformableRuntime;

[[nodiscard]] bool IsFiniteRay(const DeformablePickingRay& ray){
    const f32 minDistance = ray.minDistance();
    const f32 maxDistance = ray.maxDistance();
    return DeformableValidation::FiniteVector(LoadFloat(ray.origin()), 0x7u)
        && DeformableValidation::FiniteVector(LoadFloat(ray.direction()), 0x7u)
        && IsFinite(minDistance)
        && IsFinite(maxDistance)
        && minDistance >= 0.0f
        && minDistance <= maxDistance
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
    Float4& normal,
    Float4U& tangent,
    const Float4& fallbackNormal,
    const Float4U& fallbackTangent)
{
    SIMDVector normalVector = DeformableRuntime::Normalize(
        LoadFloat(normal),
        DeformableRuntime::Normalize(LoadFloat(fallbackNormal), VectorSet(0.0f, 0.0f, 1.0f, 0.0f))
    );
    StoreFloat(normalVector, &normal);

    const SIMDVector tangentVector = VectorSet(tangent.x, tangent.y, tangent.z, 0.0f);
    Float4 projectedTangent;
    StoreFloat(
        VectorMultiplyAdd(
            normalVector,
            VectorReplicate(-VectorGetX(Vector3Dot(tangentVector, normalVector))),
            tangentVector
        ),
        &projectedTangent
    );
    if(VectorGetX(Vector3LengthSq(LoadFloat(projectedTangent))) <= s_FrameEpsilon){
        const SIMDVector fallbackTangentVector =
            VectorSet(fallbackTangent.x, fallbackTangent.y, fallbackTangent.z, 0.0f);
        StoreFloat(
            VectorMultiplyAdd(
                normalVector,
                VectorReplicate(-VectorGetX(Vector3Dot(fallbackTangentVector, normalVector))),
                fallbackTangentVector
            ),
            &projectedTangent
        );
    }
    if(VectorGetX(Vector3LengthSq(LoadFloat(projectedTangent))) <= s_FrameEpsilon)
        StoreFloat(DeformableRuntime::FallbackTangent(normalVector), &projectedTangent);

    StoreFloat(
        DeformableRuntime::Normalize(LoadFloat(projectedTangent), DeformableRuntime::FallbackTangent(normalVector)),
        &projectedTangent
    );
    tangent.x = projectedTangent.x;
    tangent.y = projectedTangent.y;
    tangent.z = projectedTangent.z;
    const f32 handedness = Abs(tangent.w) > s_Epsilon ? tangent.w : fallbackTangent.w;
    tangent.w = handedness < 0.0f ? -1.0f : 1.0f;
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
        if(!DeformableValidation::ActiveWeight(weight))
            continue;
        if(morph.deltas.empty())
            return false;

        for(const DeformableMorphDelta& delta : morph.deltas){
            if(!DeformableValidation::ValidMorphDelta(delta, vertices.size()))
                return false;

            DeformableVertexRest& vertex = vertices[delta.vertexId];
            StoreFloat(
                VectorMultiplyAdd(LoadFloat(delta.deltaPosition), VectorReplicate(weight), LoadFloat(vertex.position)),
                &vertex.position
            );
            StoreFloat(
                VectorMultiplyAdd(LoadFloat(delta.deltaNormal), VectorReplicate(weight), LoadFloat(vertex.normal)),
                &vertex.normal
            );
            StoreFloat(
                VectorMultiplyAdd(LoadFloat(delta.deltaTangent), VectorReplicate(weight), LoadFloat(vertex.tangent)),
                &vertex.tangent
            );
        }
    }
    return true;
}

[[nodiscard]] SIMDVector TransformJointPosition(const DeformableJointMatrix& matrix, const SIMDVector positionVector){
    SIMDVector result = VectorMultiply(VectorSplatX(positionVector), LoadFloat(matrix.column0));
    result = VectorMultiplyAdd(VectorSplatY(positionVector), LoadFloat(matrix.column1), result);
    result = VectorMultiplyAdd(VectorSplatZ(positionVector), LoadFloat(matrix.column2), result);
    return VectorAdd(result, LoadFloat(matrix.column3));
}

[[nodiscard]] SIMDVector TransformJointDirection(const DeformableJointMatrix& matrix, const SIMDVector directionVector){
    SIMDVector result = VectorMultiply(VectorSplatX(directionVector), LoadFloat(matrix.column0));
    result = VectorMultiplyAdd(VectorSplatY(directionVector), LoadFloat(matrix.column1), result);
    result = VectorMultiplyAdd(VectorSplatZ(directionVector), LoadFloat(matrix.column2), result);
    return result;
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

    SIMDVector skinnedPosition = VectorZero();
    SIMDVector skinnedNormal = VectorZero();
    SIMDVector skinnedTangent = VectorZero();
    f32 totalWeight = 0.0f;

    for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
        const f32 weight = skin.weight[influenceIndex];
        const u32 joint = static_cast<u32>(skin.joint[influenceIndex]);
        if(!DeformableValidation::ActiveWeight(weight))
            continue;
        if(joint >= jointPalette->joints.size() || !IsAffineJointMatrix(jointPalette->joints[joint]))
            return false;

        const DeformableJointMatrix& matrix = jointPalette->joints[joint];
        const SIMDVector weightVector = VectorReplicate(weight);
        skinnedPosition = VectorMultiplyAdd(
            TransformJointPosition(matrix, LoadFloat(vertex.position)),
            weightVector,
            skinnedPosition
        );
        skinnedNormal = VectorMultiplyAdd(
            TransformJointDirection(matrix, LoadFloat(vertex.normal)),
            weightVector,
            skinnedNormal
        );
        skinnedTangent = VectorMultiplyAdd(
            TransformJointDirection(matrix, VectorSet(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z, 0.0f)),
            weightVector,
            skinnedTangent
        );
        totalWeight += weight;
    }

    if(!DeformableValidation::ActiveWeight(totalWeight))
        return true;

    StoreFloat(skinnedPosition, &vertex.position);
    StoreFloat(skinnedNormal, &vertex.normal);
    Float4 skinnedTangentStorage;
    StoreFloat(skinnedTangent, &skinnedTangentStorage);
    vertex.tangent.x = skinnedTangentStorage.x;
    vertex.tangent.y = skinnedTangentStorage.y;
    vertex.tangent.z = skinnedTangentStorage.z;
    return true;
}

void ApplyDisplacement(const DeformableDisplacement& displacement, DeformableVertexRest& vertex){
    if(displacement.mode != DeformableDisplacementMode::ScalarUvRamp)
        return;

    const f32 offset = Saturate(vertex.uv0.x) * displacement.amplitude;
    if(!DeformableValidation::ActiveWeight(offset))
        return;

    StoreFloat(
        VectorMultiplyAdd(LoadFloat(vertex.normal), VectorReplicate(offset), LoadFloat(vertex.position)),
        &vertex.position
    );
}

void ApplyTransform(const Core::Scene::TransformComponent* transform, DeformableVertexRest& vertex){
    if(!transform)
        return;

    SIMDVector position = VectorMultiply(LoadFloat(vertex.position), LoadFloat(transform->scale));
    position = Vector3Rotate(position, LoadFloat(transform->rotation));
    StoreFloat(VectorAdd(position, LoadFloat(transform->position)), &vertex.position);

    SIMDVector normalVector = Vector3Rotate(LoadFloat(vertex.normal), LoadFloat(transform->rotation));
    normalVector = DeformableRuntime::Normalize(normalVector, VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
    Float4 normal;
    StoreFloat(normalVector, &normal);
    vertex.normal = Float3U(normal.x, normal.y, normal.z);

    const SIMDVector tangentVector = DeformableRuntime::Normalize(
        Vector3Rotate(VectorSet(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z, 0.0f), LoadFloat(transform->rotation)),
        DeformableRuntime::FallbackTangent(normalVector)
    );
    Float4 tangent;
    StoreFloat(tangentVector, &tangent);
    vertex.tangent.x = tangent.x;
    vertex.tangent.y = tangent.y;
    vertex.tangent.z = tangent.z;
}

[[nodiscard]] bool IntersectTriangle(
    const Float4& origin,
    const Float4& direction,
    const Float4& a,
    const Float4& b,
    const Float4& c,
    f32& outDistance,
    f32 (&outBary)[3])
{
    const SIMDVector originVector = LoadFloat(origin);
    const SIMDVector directionVector = LoadFloat(direction);
    const SIMDVector aVector = LoadFloat(a);
    const SIMDVector bVector = LoadFloat(b);
    const SIMDVector cVector = LoadFloat(c);
    const SIMDVector edge0 = VectorSubtract(bVector, aVector);
    const SIMDVector edge1 = VectorSubtract(cVector, aVector);
    const SIMDVector p = Vector3Cross(directionVector, edge1);
    const f32 determinant = VectorGetX(Vector3Dot(edge0, p));
    if(Abs(determinant) <= s_Epsilon)
        return false;

    const f32 invDeterminant = 1.0f / determinant;
    const SIMDVector t = VectorSubtract(originVector, aVector);
    const f32 u = VectorGetX(Vector3Dot(t, p)) * invDeterminant;
    if(u < -s_Epsilon || u > 1.0f + s_Epsilon)
        return false;

    const SIMDVector q = Vector3Cross(t, edge0);
    const f32 v = VectorGetX(Vector3Dot(directionVector, q)) * invDeterminant;
    if(v < -s_Epsilon || (u + v) > 1.0f + s_Epsilon)
        return false;

    const f32 distance = VectorGetX(Vector3Dot(edge1, q)) * invDeterminant;
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
        const Float4 restNormal(vertex.normal.x, vertex.normal.y, vertex.normal.z);
        const Float4U restTangent = vertex.tangent;

        Float4 normal(vertex.normal.x, vertex.normal.y, vertex.normal.z);
        __hidden_deformable_picking::OrthonormalizeFrame(normal, vertex.tangent, restNormal, restTangent);
        vertex.normal = Float3U(normal.x, normal.y, normal.z);

        const Float4 preSkinNormal = normal;
        const Float4U preSkinTangent = vertex.tangent;
        if(!__hidden_deformable_picking::ApplySkin(
            instance,
            inputs.jointPalette,
            static_cast<u32>(vertexIndex),
            vertex
        ))
            return false;
        normal = Float4(vertex.normal.x, vertex.normal.y, vertex.normal.z);
        __hidden_deformable_picking::OrthonormalizeFrame(normal, vertex.tangent, preSkinNormal, preSkinTangent);
        vertex.normal = Float3U(normal.x, normal.y, normal.z);

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

bool ResolveDeformableRestSurfaceSample(
    const DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    const DeformableHitBarycentric& bary,
    SourceSample& outSample)
{
    const f32 unpackedBary[3] = { bary[0], bary[1], bary[2] };
    return ResolveDeformableRestSurfaceSample(instance, triangle, unpackedBary, outSample);
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

    const Float4 rayOrigin(ray.origin().x, ray.origin().y, ray.origin().z);
    const SIMDVector rayDirectionVector = DeformableRuntime::Normalize(
        VectorSet(ray.direction().x, ray.direction().y, ray.direction().z, 0.0f),
        VectorZero()
    );
    Float4 rayDirection;
    StoreFloat(rayDirectionVector, &rayDirection);
    if(VectorGetX(Vector3LengthSq(rayDirectionVector)) <= DeformableRuntime::s_FrameEpsilon)
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<DeformableVertexRest, Core::Alloc::ScratchAllocator<DeformableVertexRest>> posedVertices{
        Core::Alloc::ScratchAllocator<DeformableVertexRest>(scratchArena)
    };
    if(!__hidden_deformable_picking::BuildPickingVertices(instance, inputs, posedVertices))
        return false;

    const usize triangleCount = instance.indices.size() / 3u;
    bool foundHit = false;
    const f32 minDistance = ray.minDistance();
    f32 closestDistance = ray.maxDistance();
    DeformablePosedHit closestHit;
    for(usize triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex){
        u32 vertexIndices[3] = {};
        if(!DeformableRuntime::ValidateTriangleIndex(instance, static_cast<u32>(triangleIndex), vertexIndices))
            return false;

        const Float4 a(
            posedVertices[vertexIndices[0]].position.x,
            posedVertices[vertexIndices[0]].position.y,
            posedVertices[vertexIndices[0]].position.z
        );
        const Float4 b(
            posedVertices[vertexIndices[1]].position.x,
            posedVertices[vertexIndices[1]].position.y,
            posedVertices[vertexIndices[1]].position.z
        );
        const Float4 c(
            posedVertices[vertexIndices[2]].position.x,
            posedVertices[vertexIndices[2]].position.y,
            posedVertices[vertexIndices[2]].position.z
        );

        f32 distance = 0.0f;
        f32 bary[3] = {};
        if(!__hidden_deformable_picking::IntersectTriangle(rayOrigin, rayDirection, a, b, c, distance, bary))
            continue;
        if(distance < minDistance || distance > closestDistance)
            continue;

        f32 hitBary[3] = {};
        if(!DeformableValidation::NormalizeSourceBarycentric(bary, hitBary))
            continue;

        SourceSample restSample{};
        if(!ResolveDeformableRestSurfaceSample(instance, static_cast<u32>(triangleIndex), hitBary, restSample))
            continue;

        const SIMDVector aVector = LoadFloat(a);
        const SIMDVector edge0 = VectorSubtract(LoadFloat(b), aVector);
        const SIMDVector edge1 = VectorSubtract(LoadFloat(c), aVector);
        Float4 normal;
        StoreFloat(
            DeformableRuntime::Normalize(Vector3Cross(edge0, edge1), VectorSet(0.0f, 0.0f, 1.0f, 0.0f)),
            &normal
        );
        Float4 position;
        StoreFloat(
            VectorMultiplyAdd(LoadFloat(rayDirection), VectorReplicate(distance), LoadFloat(rayOrigin)),
            &position
        );

        closestDistance = distance;
        closestHit.entity = instance.entity;
        closestHit.runtimeMesh = instance.handle;
        closestHit.editRevision = instance.editRevision;
        closestHit.triangle = static_cast<u32>(triangleIndex);
        closestHit.bary[0] = hitBary[0];
        closestHit.bary[1] = hitBary[1];
        closestHit.bary[2] = hitBary[2];
        closestHit.setDistance(distance);
        closestHit.position = Float4(position.x, position.y, position.z, 1.0f);
        closestHit.normal = Float4(normal.x, normal.y, normal.z, 0.0f);
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
    f32 closestDistance = ray.maxDistance();

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
            inputs.transform = world.tryGetComponent<Core::Scene::TransformComponent>(entity);

            DeformablePosedHit hit;
            if(!RaycastDeformableRuntimeMesh(*instance, inputs, ray, hit))
                return;
            if(!foundHit || hit.distance() < closestDistance){
                closestDistance = hit.distance();
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

