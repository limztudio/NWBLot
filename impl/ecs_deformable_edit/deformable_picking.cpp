// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_picking.h"

#include <core/alloc/scratch.h>
#include <core/ecs/world.h>
#include <core/geometry/frame_math.h>
#include <impl/ecs_deformable/deformable_displacement_runtime.h>
#include <impl/ecs_render/renderer_system.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_picking{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace DeformableRuntime;

using MorphWeightLookup = HashMap<
    NameHash,
    f32,
    Hasher<NameHash>,
    EqualTo<NameHash>,
    Core::Alloc::ScratchAllocator<Pair<const NameHash, f32>>
>;

struct PreparedJointPaletteEntry{
    SIMDMatrix transform;
    SIMDMatrix normalTransform;
    SIMDVector dualQuaternionReal = QuaternionIdentity();
    SIMDVector dualQuaternionDual = VectorZero();
};

[[nodiscard]] SIMDVector LoadVertexNormal(const DeformableVertexRest& vertex){
    return LoadRestVertexNormal(vertex);
}

[[nodiscard]] SIMDVector LoadVertexTangent(const DeformableVertexRest& vertex){
    return LoadRestVertexTangent(vertex);
}

void StoreVertexFrame(const SIMDVector normal, const SIMDVector tangent, DeformableVertexRest& vertex){
    StoreFloat(normal, &vertex.normal);
    StoreFloat(tangent, &vertex.tangent);
}

void OrthonormalizeVertexFrame(
    DeformableVertexRest& vertex,
    const SIMDVector fallbackNormal,
    const SIMDVector fallbackTangent,
    SIMDVector* outNormal = nullptr,
    SIMDVector* outTangent = nullptr){
    SIMDVector normal = LoadVertexNormal(vertex);
    SIMDVector tangent = LoadVertexTangent(vertex);
    Core::Geometry::FrameOrthonormalize(normal, tangent, fallbackNormal, fallbackTangent);
    StoreVertexFrame(normal, tangent, vertex);

    if(outNormal)
        *outNormal = normal;
    if(outTangent)
        *outTangent = tangent;
}

[[nodiscard]] bool IsFiniteRay(const DeformablePickingRay& ray){
    const f32 minDistance = ray.minDistance();
    const f32 maxDistance = ray.maxDistance();
    return
        DeformableValidation::FiniteVector(ray.originVector(), 0x7u)
        && DeformableValidation::FiniteVector(ray.directionVector(), 0x7u)
        && IsFinite(minDistance)
        && IsFinite(maxDistance)
        && minDistance >= 0.0f
        && minDistance <= maxDistance
    ;
}

template<typename VertexVector>
[[nodiscard]] bool ApplyMorphs(const DeformableRuntimeMeshInstance& instance, const DeformableMorphWeightsComponent* weights, VertexVector& vertices){
    if(!HasMorphWeights(weights))
        return true;

    Core::Alloc::ScratchArena<> scratchArena;
    MorphWeightLookup resolvedWeights(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        Core::Alloc::ScratchAllocator<Pair<const NameHash, f32>>(scratchArena)
    );
    Name failedMorph = NAME_NONE;
    if(!BuildMorphWeightSumLookup(instance.morphs, weights, resolvedWeights, failedMorph))
        return false;

    const usize vertexCount = vertices.size();
    for(const DeformableMorph& morph : instance.morphs){
        f32 weight = 0.0f;
        if(morph.name){
            const auto iterWeight = resolvedWeights.find(morph.name.hash());
            if(iterWeight != resolvedWeights.end())
                weight = iterWeight.value();
        }
        if(!DeformableValidation::ActiveWeight(weight))
            continue;
        if(morph.deltas.empty())
            return false;

        const SIMDVector weightVector = VectorReplicate(weight);
        for(const DeformableMorphDelta& delta : morph.deltas){
            if(!DeformableValidation::ValidMorphDelta(delta, vertexCount))
                return false;

            DeformableVertexRest& vertex = vertices[delta.vertexId];
            StoreFloat(
                VectorMultiplyAdd(LoadFloat(delta.deltaPosition), weightVector, LoadRestVertexPosition(vertex)),
                &vertex.position
            );
            StoreFloat(
                VectorMultiplyAdd(LoadFloat(delta.deltaNormal), weightVector, LoadRestVertexNormal(vertex)),
                &vertex.normal
            );
            StoreFloat(
                VectorMultiplyAdd(LoadFloat(delta.deltaTangent), weightVector, LoadRestVertexTangent(vertex)),
                &vertex.tangent
            );
        }
    }
    return true;
}

[[nodiscard]] SIMDVector TransformJointPosition(const SIMDMatrix& matrix, const SIMDVector positionVector){
    SIMDVector result = VectorMultiply(VectorSplatX(positionVector), matrix.v[0]);
    result = VectorMultiplyAdd(VectorSplatY(positionVector), matrix.v[1], result);
    result = VectorMultiplyAdd(VectorSplatZ(positionVector), matrix.v[2], result);
    return VectorAdd(result, matrix.v[3]);
}

[[nodiscard]] SIMDVector TransformJointDirection(const SIMDMatrix& matrix, const SIMDVector directionVector){
    SIMDVector result = VectorMultiply(VectorSplatX(directionVector), matrix.v[0]);
    result = VectorMultiplyAdd(VectorSplatY(directionVector), matrix.v[1], result);
    result = VectorMultiplyAdd(VectorSplatZ(directionVector), matrix.v[2], result);
    return result;
}

template<typename SourceJointVector, typename PreparedJointPaletteVector>
[[nodiscard]] bool BuildPreparedJointPaletteFromJointMatrices(
    const DeformableRuntimeMeshInstance& instance,
    const SourceJointVector& sourceJoints,
    const u32 skinningMode,
    PreparedJointPaletteVector& outJointPalette){
    outJointPalette.clear();
    if(instance.skin.empty() || sourceJoints.empty())
        return true;
    if(!ValidDeformableSkinningMode(skinningMode))
        return false;

    const usize jointCount = sourceJoints.size();
    outJointPalette.reserve(jointCount);
    const bool requiresDualQuaternion = skinningMode == DeformableSkinningMode::DualQuaternion;
    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        PreparedJointPaletteEntry entry;
        if(
            !DeformableRuntime::ResolveSkinningJointMatrix(
                instance,
                static_cast<u32>(jointIndex),
                sourceJoints[jointIndex],
                entry.transform
            )
            || !TryBuildJointNormalMatrix(entry.transform, entry.normalTransform)
        )
            return false;
        if(
            requiresDualQuaternion
            && !TryBuildJointDualQuaternion(
                    entry.transform,
                    entry.dualQuaternionReal,
                    entry.dualQuaternionDual
                )
        )
            return false;

        outJointPalette.push_back(entry);
    }
    return true;
}

template<typename PreparedJointPaletteVector>
[[nodiscard]] bool BuildPreparedJointPalette(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableJointPaletteComponent* jointPalette,
    PreparedJointPaletteVector& outJointPalette){
    outJointPalette.clear();
    if(!jointPalette)
        return true;

    return BuildPreparedJointPaletteFromJointMatrices(
        instance,
        jointPalette->joints,
        jointPalette->skinningMode,
        outJointPalette
    );
}

template<typename PreparedJointPaletteVector>
[[nodiscard]] bool ApplySkin(
    const DeformableRuntimeMeshInstance& instance,
    const PreparedJointPaletteVector& jointPalette,
    const u32 skinningMode,
    const u32 vertexId,
    DeformableVertexRest& vertex){
    if(jointPalette.empty())
        return true;
    if(instance.skin.empty())
        return true;
    if(instance.skin.size() != instance.restVertices.size() || vertexId >= instance.skin.size())
        return false;

    const SkinInfluence4& skin = instance.skin[vertexId];
    if(!DeformableValidation::ValidSkinInfluence(skin))
        return false;
    u32 failedJoint = 0u;
    if(!DeformableValidation::SkinInfluenceFitsSkeleton(skin, instance.skeletonJointCount, failedJoint))
        return false;

    const usize jointCount = jointPalette.size();
    const bool useDualQuaternion = skinningMode == DeformableSkinningMode::DualQuaternion;
    if(useDualQuaternion){
        SIMDVector blendedReal = VectorZero();
        SIMDVector blendedDual = VectorZero();
        SIMDVector referenceReal = QuaternionIdentity();
        bool hasReference = false;
        f32 totalDualQuaternionWeight = 0.0f;

        for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
            const f32 weight = skin.weight[influenceIndex];
            const u32 joint = static_cast<u32>(skin.joint[influenceIndex]);
            if(!DeformableValidation::ActiveWeight(weight))
                continue;
            if(joint >= jointCount)
                return false;

            const PreparedJointPaletteEntry& jointEntry = jointPalette[joint];
            SIMDVector real = jointEntry.dualQuaternionReal;
            SIMDVector dual = jointEntry.dualQuaternionDual;
            if(!hasReference){
                referenceReal = real;
                hasReference = true;
            }
            else if(VectorGetX(QuaternionDot(referenceReal, real)) < 0.0f){
                real = VectorNegate(real);
                dual = VectorNegate(dual);
            }

            const SIMDVector weightVector = VectorReplicate(weight);
            blendedReal = VectorMultiplyAdd(real, weightVector, blendedReal);
            blendedDual = VectorMultiplyAdd(dual, weightVector, blendedDual);
            totalDualQuaternionWeight += weight;
        }

        if(!DeformableValidation::ActiveWeight(totalDualQuaternionWeight))
            return true;
        if(!NormalizeBlendedDualQuaternion(blendedReal, blendedDual))
            return false;

        const SIMDVector basePosition = LoadRestVertexPosition(vertex);
        const SIMDVector baseNormal = LoadRestVertexNormal(vertex);
        const SIMDVector baseTangent = VectorSetW(LoadRestVertexTangent(vertex), 0.0f);
        StoreFloat(TransformDualQuaternionPosition(blendedReal, blendedDual, basePosition), &vertex.position);
        StoreFloat(TransformDualQuaternionDirection(blendedReal, baseNormal), &vertex.normal);
        StoreFloat(
            VectorSetW(TransformDualQuaternionDirection(blendedReal, baseTangent), vertex.tangent.w),
            &vertex.tangent
        );
        return true;
    }

    SIMDVector skinnedPosition = VectorZero();
    SIMDVector skinnedNormal = VectorZero();
    SIMDVector skinnedTangent = VectorZero();
    const SIMDVector basePosition = LoadRestVertexPosition(vertex);
    const SIMDVector baseNormal = LoadRestVertexNormal(vertex);
    const SIMDVector baseTangent = VectorSetW(LoadRestVertexTangent(vertex), 0.0f);
    f32 totalWeight = 0.0f;

    for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
        const f32 weight = skin.weight[influenceIndex];
        const u32 joint = static_cast<u32>(skin.joint[influenceIndex]);
        if(!DeformableValidation::ActiveWeight(weight))
            continue;
        if(joint >= jointCount)
            return false;

        const PreparedJointPaletteEntry& jointEntry = jointPalette[joint];

        const SIMDVector weightVector = VectorReplicate(weight);
        const SIMDVector transformedNormal = TransformJointDirection(jointEntry.normalTransform, baseNormal);
        if(!DeformableValidation::FiniteVector(transformedNormal, 0x7u))
            return false;
        skinnedPosition = VectorMultiplyAdd(
            TransformJointPosition(jointEntry.transform, basePosition),
            weightVector,
            skinnedPosition
        );
        skinnedNormal = VectorMultiplyAdd(
            transformedNormal,
            weightVector,
            skinnedNormal
        );
        skinnedTangent = VectorMultiplyAdd(
            TransformJointDirection(jointEntry.transform, baseTangent),
            weightVector,
            skinnedTangent
        );
        totalWeight += weight;
    }

    if(!DeformableValidation::ActiveWeight(totalWeight))
        return true;

    StoreFloat(skinnedPosition, &vertex.position);
    StoreFloat(skinnedNormal, &vertex.normal);
    StoreFloat(VectorSetW(skinnedTangent, vertex.tangent.w), &vertex.tangent);
    return true;
}

[[nodiscard]] bool ResolvePickingDisplacementTexture(
    const DeformableDisplacement& displacement,
    Core::Assets::AssetManager* assetManager,
    const DeformableDisplacementTexture* inputTexture,
    UniquePtr<Core::Assets::IAsset>& outLoadedAsset,
    const DeformableDisplacementTexture*& outTexture){
    outLoadedAsset.reset();
    outTexture = inputTexture;
    if(!DeformableDisplacementModeUsesTexture(displacement.mode))
        return true;
    if(ValidateDisplacementTexture(displacement, outTexture))
        return true;
    if(!assetManager)
        return false;

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!assetManager->loadSync(DeformableDisplacementTexture::AssetTypeName(), displacement.texture.name(), loadedAsset))
        return false;
    if(!loadedAsset || loadedAsset->assetType() != DeformableDisplacementTexture::AssetTypeName())
        return false;

    outLoadedAsset = Move(loadedAsset);
    outTexture = checked_cast<const DeformableDisplacementTexture*>(outLoadedAsset.get());
    return ValidateDisplacementTexture(displacement, outTexture);
}

void ApplyScalarTextureNormal(
    const DeformableDisplacement& displacement,
    const DeformableDisplacementTexture& texture,
    DeformableVertexRest& vertex){
    if(texture.width() <= 1u && texture.height() <= 1u)
        return;

    const Float2U uv = DisplacementTextureCoord(displacement, vertex.uv0);
    const f32 du = DisplacementTextureCoordStep(texture.width());
    const f32 dv = DisplacementTextureCoordStep(texture.height());
    const Float4U right = SampleDisplacementTextureCoord(texture, Float2U(Saturate(uv.x + du), uv.y));
    const Float4U left = SampleDisplacementTextureCoord(texture, Float2U(Saturate(uv.x - du), uv.y));
    const Float4U up = SampleDisplacementTextureCoord(texture, Float2U(uv.x, Saturate(uv.y + dv)));
    const Float4U down = SampleDisplacementTextureCoord(texture, Float2U(uv.x, Saturate(uv.y - dv)));
    const f32 heightU = (right.x - left.x) * displacement.amplitude * 0.5f;
    const f32 heightV = (up.x - down.x) * displacement.amplitude * 0.5f;
    if(!IsFinite(heightU) || !IsFinite(heightV))
        return;

    SIMDVector normal = LoadRestVertexNormal(vertex);
    SIMDVector tangent = VectorSetW(LoadRestVertexTangent(vertex), 0.0f);
    const SIMDVector bitangent = VectorMultiply(
        Core::Geometry::FrameResolveBitangent(normal, tangent, VectorSet(0.0f, 1.0f, 0.0f, 0.0f)),
        VectorReplicate(Core::Geometry::FrameTangentHandedness(vertex.tangent.w, 1.0f))
    );
    normal = Core::Geometry::FrameNormalizeDirection(
        VectorSubtract(
            VectorSubtract(normal, VectorScale(tangent, heightU)),
            VectorScale(bitangent, heightV)
        ),
        normal
    );
    tangent = Core::Geometry::FrameResolveTangent(normal, tangent, tangent);
    StoreFloat(normal, &vertex.normal);
    StoreFloat(VectorSetW(tangent, vertex.tangent.w), &vertex.tangent);
}

void ApplyVectorTextureNormal(
    const DeformableDisplacement& displacement,
    const DeformableDisplacementTexture& texture,
    DeformableVertexRest& vertex){
    if(texture.width() <= 1u && texture.height() <= 1u)
        return;

    const Float2U uv = DisplacementTextureCoord(displacement, vertex.uv0);
    const f32 du = DisplacementTextureCoordStep(texture.width());
    const f32 dv = DisplacementTextureCoordStep(texture.height());
    const SIMDVector normal = LoadRestVertexNormal(vertex);
    const SIMDVector tangentWithHandedness = LoadRestVertexTangent(vertex);
    const SIMDVector tangent = VectorSetW(tangentWithHandedness, 0.0f);
    const SIMDVector right = VectorTextureOffsetToFrame(
        displacement,
        displacement.mode,
        SampleDisplacementTextureCoord(texture, Float2U(Saturate(uv.x + du), uv.y)),
        normal,
        tangentWithHandedness
    );
    const SIMDVector left = VectorTextureOffsetToFrame(
        displacement,
        displacement.mode,
        SampleDisplacementTextureCoord(texture, Float2U(Saturate(uv.x - du), uv.y)),
        normal,
        tangentWithHandedness
    );
    const SIMDVector up = VectorTextureOffsetToFrame(
        displacement,
        displacement.mode,
        SampleDisplacementTextureCoord(texture, Float2U(uv.x, Saturate(uv.y + dv))),
        normal,
        tangentWithHandedness
    );
    const SIMDVector down = VectorTextureOffsetToFrame(
        displacement,
        displacement.mode,
        SampleDisplacementTextureCoord(texture, Float2U(uv.x, Saturate(uv.y - dv))),
        normal,
        tangentWithHandedness
    );
    if(
        !DeformableValidation::FiniteVector(right, 0x7u)
        || !DeformableValidation::FiniteVector(left, 0x7u)
        || !DeformableValidation::FiniteVector(up, 0x7u)
        || !DeformableValidation::FiniteVector(down, 0x7u)
    )
        return;

    const SIMDVector derivativeU = VectorScale(VectorSubtract(right, left), 0.5f);
    const SIMDVector derivativeV = VectorScale(VectorSubtract(up, down), 0.5f);
    const f32 handedness = Core::Geometry::FrameTangentHandedness(vertex.tangent.w, 1.0f);
    const SIMDVector bitangent = VectorMultiply(
        Core::Geometry::FrameResolveBitangent(normal, tangent, VectorSet(0.0f, 1.0f, 0.0f, 0.0f)),
        VectorReplicate(handedness)
    );
    const SIMDVector displacedTangent = VectorAdd(tangent, derivativeU);
    const SIMDVector displacedBitangent = VectorAdd(bitangent, derivativeV);
    SIMDVector adjustedNormal = VectorScale(
        Vector3Cross(displacedTangent, displacedBitangent),
        handedness
    );
    adjustedNormal = Core::Geometry::FrameNormalizeDirection(adjustedNormal, normal);
    const SIMDVector adjustedTangent = Core::Geometry::FrameResolveTangent(adjustedNormal, displacedTangent, tangent);
    StoreFloat(adjustedNormal, &vertex.normal);
    StoreFloat(VectorSetW(adjustedTangent, handedness), &vertex.tangent);
}

void ApplyDisplacement(
    const DeformableDisplacement& displacement,
    const DeformableDisplacementTexture* texture,
    DeformableVertexRest& vertex){
    if(displacement.mode == DeformableDisplacementMode::None)
        return;

    f32 scalarOffset = 0.0f;
    SIMDVector worldOffset = VectorZero();
    if(displacement.mode == DeformableDisplacementMode::ScalarUvRamp){
        scalarOffset = Saturate(vertex.uv0.x) * displacement.amplitude;
    }
    else{
        if(!texture)
            return;

        const Float4U sample = SampleDisplacementTexture(displacement, *texture, vertex.uv0);
        if(displacement.mode == DeformableDisplacementMode::ScalarTexture){
            scalarOffset = (sample.x + displacement.bias) * displacement.amplitude;
        }
        else{
            worldOffset = VectorTextureOffsetToFrame(
                displacement,
                displacement.mode,
                sample,
                LoadRestVertexNormal(vertex),
                LoadRestVertexTangent(vertex)
            );
        }
    }

    if(
        displacement.mode == DeformableDisplacementMode::ScalarUvRamp
        || displacement.mode == DeformableDisplacementMode::ScalarTexture
    ){
        const SIMDVector displacementNormal = LoadRestVertexNormal(vertex);
        if(displacement.mode == DeformableDisplacementMode::ScalarTexture && texture)
            ApplyScalarTextureNormal(displacement, *texture, vertex);
        if(!DeformableValidation::ActiveWeight(scalarOffset))
            return;

        StoreFloat(
            VectorMultiplyAdd(displacementNormal, VectorReplicate(scalarOffset), LoadRestVertexPosition(vertex)),
            &vertex.position
        );
        return;
    }

    if(!DeformableValidation::FiniteVector(worldOffset, 0x7u))
        return;
    if(texture)
        ApplyVectorTextureNormal(displacement, *texture, vertex);
    if(!DeformableValidation::ActiveWeight(VectorGetX(Vector3LengthSq(worldOffset))))
        return;

    StoreFloat(
        VectorAdd(LoadRestVertexPosition(vertex), worldOffset),
        &vertex.position
    );
}

void ApplyTransform(const Core::ECSTransform::TransformComponent* transform, DeformableVertexRest& vertex){
    if(!transform)
        return;

    const SIMDVector rotation = LoadFloat(transform->rotation);
    SIMDVector position = VectorMultiply(LoadRestVertexPosition(vertex), LoadFloat(transform->scale));
    position = Vector3Rotate(position, rotation);
    StoreFloat(VectorAdd(position, LoadFloat(transform->position)), &vertex.position);

    SIMDVector normalVector = Vector3Rotate(LoadRestVertexNormal(vertex), rotation);
    normalVector = Core::Geometry::FrameNormalizeDirection(normalVector, VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
    StoreFloat(normalVector, &vertex.normal);

    const SIMDVector tangentVector = Core::Geometry::FrameNormalizeDirection(
        Vector3Rotate(VectorSetW(LoadRestVertexTangent(vertex), 0.0f), rotation),
        Core::Geometry::FrameFallbackTangent(normalVector)
    );
    StoreFloat(VectorSetW(tangentVector, vertex.tangent.w), &vertex.tangent);
}

[[nodiscard]] bool IntersectTriangle(
    const SIMDVector originVector,
    const SIMDVector directionVector,
    const SIMDVector aVector,
    const SIMDVector bVector,
    const SIMDVector cVector,
    f32& outDistance,
    f32 (&outBary)[3]){
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
[[nodiscard]] bool BuildPickingVertices(const DeformableRuntimeMeshInstance& instance, const DeformablePickingInputs& inputs, VertexVector& outVertices){
    outVertices.clear();
    if(!ValidRuntimeMeshPayloadArrays(instance))
        return false;

    DeformableDisplacement displacement;
    if(!DeformableRuntime::ResolveEffectiveDisplacement(instance.displacement, inputs.displacement, displacement))
        return false;
    UniquePtr<Core::Assets::IAsset> loadedDisplacementTextureAsset;
    const DeformableDisplacementTexture* displacementTexture = nullptr;
    if(
        !__hidden_deformable_picking::ResolvePickingDisplacementTexture(
            displacement,
            inputs.assetManager,
            inputs.displacementTexture,
            loadedDisplacementTextureAsset,
            displacementTexture
        )
    )
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<
        __hidden_deformable_picking::PreparedJointPaletteEntry,
        Core::Alloc::ScratchAllocator<__hidden_deformable_picking::PreparedJointPaletteEntry>
    > jointPalette{ Core::Alloc::ScratchAllocator<__hidden_deformable_picking::PreparedJointPaletteEntry>(scratchArena) };
    u32 skinningMode = inputs.jointPalette
        ? inputs.jointPalette->skinningMode
        : DeformableSkinningMode::LinearBlend
    ;
    if(DeformableRuntime::HasSkeletonPose(inputs.skeletonPose)){
        Vector<DeformableJointMatrix, Core::Alloc::ScratchAllocator<DeformableJointMatrix>> poseJoints{
            Core::Alloc::ScratchAllocator<DeformableJointMatrix>(scratchArena)
        };
        if(!DeformableRuntime::BuildJointPaletteFromSkeletonPose(*inputs.skeletonPose, poseJoints, skinningMode))
            return false;
        if(
            !__hidden_deformable_picking::BuildPreparedJointPaletteFromJointMatrices(
                instance,
                poseJoints,
                skinningMode,
                jointPalette
            )
        )
            return false;
    }
    else{
        if(!__hidden_deformable_picking::BuildPreparedJointPalette(instance, inputs.jointPalette, jointPalette))
            return false;
    }

    AssignTriviallyCopyableVector(outVertices, instance.restVertices);

    if(!__hidden_deformable_picking::ApplyMorphs(instance, inputs.morphWeights, outVertices))
        return false;

    for(usize vertexIndex = 0; vertexIndex < outVertices.size(); ++vertexIndex){
        DeformableVertexRest& vertex = outVertices[vertexIndex];
        const SIMDVector restNormal = __hidden_deformable_picking::LoadVertexNormal(vertex);
        const SIMDVector restTangent = __hidden_deformable_picking::LoadVertexTangent(vertex);

        SIMDVector preSkinNormal;
        SIMDVector preSkinTangent;
        __hidden_deformable_picking::OrthonormalizeVertexFrame(vertex, restNormal, restTangent, &preSkinNormal, &preSkinTangent);

        if(
            !__hidden_deformable_picking::ApplySkin(
                instance,
                jointPalette,
                skinningMode,
                static_cast<u32>(vertexIndex),
                vertex
            )
        )
            return false;
        __hidden_deformable_picking::OrthonormalizeVertexFrame(vertex, preSkinNormal, preSkinTangent);

        const SIMDVector preDisplacementNormal = __hidden_deformable_picking::LoadVertexNormal(vertex);
        const SIMDVector preDisplacementTangent = __hidden_deformable_picking::LoadVertexTangent(vertex);
        __hidden_deformable_picking::ApplyDisplacement(displacement, displacementTexture, vertex);
        __hidden_deformable_picking::OrthonormalizeVertexFrame(vertex, preDisplacementNormal, preDisplacementTangent);
        __hidden_deformable_picking::ApplyTransform(inputs.transform, vertex);
        if(!DeformableValidation::ValidRestVertexFrame(vertex))
            return false;
    }

    return true;
}

template<typename VertexVector>
[[nodiscard]] bool BuildPickingVerticesIfReady(const DeformableRuntimeMeshInstance& instance, const DeformablePickingInputs& inputs, VertexVector& outVertices){
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
    Vector<DeformableVertexRest>& outVertices){
    return __hidden_deformable_picking::BuildPickingVerticesIfReady(instance, inputs, outVertices);
}

bool BuildDeformablePickingVertices(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    Vector<DeformableVertexRest, Core::Alloc::ScratchAllocator<DeformableVertexRest>>& outVertices){
    return __hidden_deformable_picking::BuildPickingVerticesIfReady(instance, inputs, outVertices);
}

bool ResolveDeformableRestSurfaceSample(
    const DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    const f32 (&bary)[3],
    SourceSample& outSample){
    outSample = SourceSample{};
    if(!DeformableValidation::ValidLooseBarycentric(bary))
        return false;
    if(instance.indices.empty() || (instance.indices.size() % 3u) != 0u)
        return false;

    u32 vertexIndices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, triangle, vertexIndices))
        return false;

    if(instance.sourceSamples.empty())
        return false;
    if(instance.sourceSamples.size() != instance.restVertices.size())
        return false;
    if(instance.sourceTriangleCount == 0u)
        return false;

    const SourceSample& sample0 = instance.sourceSamples[vertexIndices[0]];
    const SourceSample& sample1 = instance.sourceSamples[vertexIndices[1]];
    const SourceSample& sample2 = instance.sourceSamples[vertexIndices[2]];
    if(
        !DeformableValidation::ValidSourceSample(sample0, instance.sourceTriangleCount)
        || !DeformableValidation::ValidSourceSample(sample1, instance.sourceTriangleCount)
        || !DeformableValidation::ValidSourceSample(sample2, instance.sourceTriangleCount)
    )
        return false;
    if(sample0.sourceTri != sample1.sourceTri || sample0.sourceTri != sample2.sourceTri)
        return false;

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
    SourceSample& outSample){
    const f32 unpackedBary[3] = { bary[0], bary[1], bary[2] };
    return ResolveDeformableRestSurfaceSample(instance, triangle, unpackedBary, outSample);
}

DeformableEditMaskFlags ResolveDeformableTriangleEditMask(const DeformableRuntimeMeshInstance& instance, const u32 triangle){
    const usize triangleCount = instance.indices.size() / 3u;
    if(static_cast<usize>(triangle) >= triangleCount)
        return DeformableEditMaskFlag::Forbidden;
    if(instance.editMaskPerTriangle.empty())
        return s_DeformableEditMaskDefault;
    if(instance.editMaskPerTriangle.size() != triangleCount)
        return DeformableEditMaskFlag::Forbidden;

    const DeformableEditMaskFlags flags = instance.editMaskPerTriangle[triangle];
    return
        ValidDeformableEditMaskFlags(flags)
            ? flags
            : static_cast<DeformableEditMaskFlags>(DeformableEditMaskFlag::Forbidden)
    ;
}

bool RaycastDeformableRuntimeMesh(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    const DeformablePickingRay& ray,
    DeformablePosedHit& outHit){
    outHit = DeformablePosedHit{};
    if(!instance.entity.valid() || !instance.handle.valid() || !__hidden_deformable_picking::IsFiniteRay(ray))
        return false;
    if((instance.dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u)
        return false;

    const SIMDVector rayOriginVector = ray.originVector();
    const SIMDVector rayDirectionVector = Core::Geometry::FrameNormalizeDirection(ray.directionVector(), VectorZero());
    if(!Core::Geometry::FrameValidDirection(rayDirectionVector))
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
        const usize indexBase = triangleIndex * 3u;
        const u32 vertexIndices[3] = {
            instance.indices[indexBase + 0u],
            instance.indices[indexBase + 1u],
            instance.indices[indexBase + 2u]
        };

        const SIMDVector aVector = LoadRestVertexPosition(posedVertices[vertexIndices[0]]);
        const SIMDVector bVector = LoadRestVertexPosition(posedVertices[vertexIndices[1]]);
        const SIMDVector cVector = LoadRestVertexPosition(posedVertices[vertexIndices[2]]);

        f32 distance = 0.0f;
        f32 bary[3] = {};
        if(!__hidden_deformable_picking::IntersectTriangle(rayOriginVector, rayDirectionVector, aVector, bVector, cVector, distance, bary))
            continue;
        if(distance < minDistance || distance > closestDistance)
            continue;

        f32 hitBary[3] = {};
        if(!DeformableValidation::NormalizeSourceBarycentric(bary, hitBary))
            continue;

        SourceSample restSample{};
        if(!ResolveDeformableRestSurfaceSample(instance, static_cast<u32>(triangleIndex), hitBary, restSample))
            continue;

        const SIMDVector edge0 = VectorSubtract(bVector, aVector);
        const SIMDVector edge1 = VectorSubtract(cVector, aVector);
        const SIMDVector normal = Core::Geometry::FrameNormalizeDirection(Vector3Cross(edge0, edge1), VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        const SIMDVector position = VectorMultiplyAdd(rayDirectionVector, VectorReplicate(distance), rayOriginVector);

        closestDistance = distance;
        closestHit.entity = instance.entity;
        closestHit.runtimeMesh = instance.handle;
        closestHit.editRevision = instance.editRevision;
        closestHit.triangle = static_cast<u32>(triangleIndex);
        closestHit.bary[0] = hitBary[0];
        closestHit.bary[1] = hitBary[1];
        closestHit.bary[2] = hitBary[2];
        closestHit.setDistance(distance);
        closestHit.setPosition(position);
        closestHit.setNormal(normal);
        closestHit.editMaskFlags = ResolveDeformableTriangleEditMask(instance, closestHit.triangle);
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
    DeformablePosedHit& outHit,
    Core::Assets::AssetManager* assetManager){
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
            inputs.assetManager = assetManager;
            inputs.morphWeights = world.tryGetComponent<DeformableMorphWeightsComponent>(entity);
            inputs.jointPalette = world.tryGetComponent<DeformableJointPaletteComponent>(entity);
            inputs.skeletonPose = world.tryGetComponent<DeformableSkeletonPoseComponent>(entity);
            inputs.displacement = world.tryGetComponent<DeformableDisplacementComponent>(entity);
            inputs.transform = world.tryGetComponent<Core::ECSTransform::TransformComponent>(entity);

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

