// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_surface_edit.h"

#include "deformable_runtime_helpers.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_manager.h>
#include <core/ecs/entity.h>
#include <core/ecs/world.h>
#include <core/geometry/attribute_transfer.h>
#include <core/geometry/frame_math.h>
#include <core/geometry/mesh_topology.h>
#include <core/geometry/surface_patch_edit.h>
#include <core/geometry/tangent_frame_rebuild.h>
#include <impl/assets_graphics/deformable_geometry_validation.h>
#include <impl/assets_graphics/geometry_asset.h>
#include <impl/assets_graphics/material_asset.h>
#include <global/binary.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_surface_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace DeformableRuntime;
using EdgeRecord = Core::Geometry::MeshTopologyEdge;

static constexpr f32 s_WallInnerInpaintWeights[3] = { 0.25f, 0.5f, 0.25f };
static constexpr u32 s_SurfaceEditStateMagic = 0x53454631u; // SEF1
static constexpr u32 s_SurfaceEditStateVersionV4 = 4u;
static constexpr u32 s_SurfaceEditStateVersion = s_SurfaceEditStateVersionV4;
static constexpr u32 s_MinWallLoopVertexCount = 3u;
static constexpr u32 s_MaxWallLoopCutCount = 8u;

struct HoleFrame{
    SIMDVector center = VectorZero();
    SIMDVector normal = VectorZero();
    SIMDVector tangent = VectorZero();
    SIMDVector bitangent = VectorZero();
};

[[nodiscard]] bool FiniteVec3(const SIMDVector value){
    return DeformableValidation::FiniteVector(value, 0x7u);
}

void ResolveTangentBitangentVectors(
    const SIMDVector normalVector,
    const SIMDVector tangentVector,
    const SIMDVector fallbackTangent,
    SIMDVector& outTangentVector,
    SIMDVector& outBitangentVector)
{
    outTangentVector = Core::Geometry::FrameResolveTangent(normalVector, tangentVector, fallbackTangent);
    outBitangentVector = Core::Geometry::FrameResolveBitangent(normalVector, outTangentVector, s_SIMDIdentityR1);
}

struct SurfaceEditStateHeaderV4{
    u32 magic = s_SurfaceEditStateMagic;
    u32 version = s_SurfaceEditStateVersionV4;
    u64 editCount = 0;
    u64 accessoryCount = 0;
    u64 stringTableByteCount = 0;
};
static_assert(IsStandardLayout_V<SurfaceEditStateHeaderV4>, "SurfaceEditStateHeaderV4 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SurfaceEditStateHeaderV4>, "SurfaceEditStateHeaderV4 must stay binary-serializable");

struct SurfaceEditAccessoryRecordBinaryV4{
    DeformableSurfaceEditId anchorEditId = 0;
    u32 firstWallVertex = Limit<u32>::s_Max;
    u32 wallVertexCount = 0;
    f32 normalOffset = 0.0f;
    f32 uniformScale = 1.0f;
    u32 geometryPathOffset = Limit<u32>::s_Max;
    u32 materialPathOffset = Limit<u32>::s_Max;
};
static_assert(
    IsStandardLayout_V<SurfaceEditAccessoryRecordBinaryV4>,
    "SurfaceEditAccessoryRecordBinaryV4 must stay binary-serializable"
);
static_assert(
    IsTriviallyCopyable_V<SurfaceEditAccessoryRecordBinaryV4>,
    "SurfaceEditAccessoryRecordBinaryV4 must stay binary-serializable"
);

using MorphDeltaLookup = HashMap<
    u32,
    usize,
    Hasher<u32>,
    EqualTo<u32>,
    Core::Alloc::ScratchAllocator<Pair<const u32, usize>>
>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVector BarycentricPoint(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&indices)[3],
    const f32 (&bary)[3])
{
    SIMDVector position = VectorScale(LoadFloat(instance.restVertices[indices[0]].position), bary[0]);
    position = VectorMultiplyAdd(LoadFloat(instance.restVertices[indices[1]].position), VectorReplicate(bary[1]), position);
    position = VectorMultiplyAdd(LoadFloat(instance.restVertices[indices[2]].position), VectorReplicate(bary[2]), position);
    return position;
}

[[nodiscard]] SIMDVector TriangleCentroid(const DeformableRuntimeMeshInstance& instance, const u32 (&indices)[3]){
    SIMDVector centroid = VectorAdd(
        VectorAdd(LoadFloat(instance.restVertices[indices[0]].position), LoadFloat(instance.restVertices[indices[1]].position)),
        LoadFloat(instance.restVertices[indices[2]].position)
    );
    return VectorScale(centroid, 1.0f / 3.0f);
}

[[nodiscard]] bool BuildHoleFrame(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&triangleIndices)[3],
    const f32 (&bary)[3],
    HoleFrame& outFrame)
{
    const SIMDVector a = LoadFloat(instance.restVertices[triangleIndices[0]].position);
    const SIMDVector b = LoadFloat(instance.restVertices[triangleIndices[1]].position);
    const SIMDVector c = LoadFloat(instance.restVertices[triangleIndices[2]].position);
    const SIMDVector edge0 = VectorSubtract(b, a);
    const SIMDVector edge1 = VectorSubtract(c, a);

    const SIMDVector rawNormal = Vector3Cross(edge0, edge1);
    if(VectorGetX(Vector3LengthSq(rawNormal)) <= Core::Geometry::s_FrameDirectionEpsilon)
        return false;

    outFrame.center = BarycentricPoint(instance, triangleIndices, bary);
    outFrame.normal = Core::Geometry::FrameNormalizeDirection(rawNormal, VectorSet(0.0f, 0.0f, 1.0f, 0.0f));

    const DeformableVertexRest& vertex0 = instance.restVertices[triangleIndices[0]];
    const DeformableVertexRest& vertex1 = instance.restVertices[triangleIndices[1]];
    const DeformableVertexRest& vertex2 = instance.restVertices[triangleIndices[2]];
    SIMDVector tangentVector = VectorScale(VectorSetW(LoadFloat(vertex0.tangent), 0.0f), bary[0]);
    tangentVector = VectorMultiplyAdd(
        VectorSetW(LoadFloat(vertex1.tangent), 0.0f),
        VectorReplicate(bary[1]),
        tangentVector
    );
    tangentVector = VectorMultiplyAdd(
        VectorSetW(LoadFloat(vertex2.tangent), 0.0f),
        VectorReplicate(bary[2]),
        tangentVector
    );
    ResolveTangentBitangentVectors(outFrame.normal, tangentVector, edge0, outFrame.tangent, outFrame.bitangent);
    return VectorGetX(Vector3LengthSq(outFrame.normal)) > Core::Geometry::s_FrameDirectionEpsilon
        && VectorGetX(Vector3LengthSq(outFrame.tangent)) > Core::Geometry::s_FrameDirectionEpsilon
        && VectorGetX(Vector3LengthSq(outFrame.bitangent)) > Core::Geometry::s_FrameDirectionEpsilon
        && Abs(VectorGetX(Vector3Dot(outFrame.normal, outFrame.tangent))) <= 0.001f
    ;
}

[[nodiscard]] bool ValidateRuntimePayload(const DeformableRuntimeMeshInstance& instance){
    if(!instance.entity.valid() || !instance.handle.valid() || instance.restVertices.empty() || instance.indices.empty())
        return false;
    if(!ValidDeformableDisplacementDescriptor(instance.displacement))
        return false;
    return DeformableValidation::ValidRuntimePayloadArrays(
        instance.restVertices,
        instance.indices,
        instance.sourceTriangleCount,
        instance.skeletonJointCount,
        instance.skin,
        instance.sourceSamples,
        instance.editMaskPerTriangle,
        instance.morphs
    );
}

[[nodiscard]] DeformableSurfaceEditPermission::Enum ResolveSurfaceEditPermission(const DeformableEditMaskFlags flags){
    if(!ValidDeformableEditMaskFlags(flags) || (flags & DeformableEditMaskFlag::Forbidden) != 0u)
        return DeformableSurfaceEditPermission::Forbidden;
    if((flags & (DeformableEditMaskFlag::Restricted | DeformableEditMaskFlag::RequiresRepair)) != 0u)
        return DeformableSurfaceEditPermission::Restricted;
    return DeformableSurfaceEditPermission::Allowed;
}

[[nodiscard]] bool MatchingSourceSample(const SourceSample& lhs, const SourceSample& rhs){
    return lhs.sourceTri == rhs.sourceTri
        && Abs(lhs.bary[0] - rhs.bary[0]) <= DeformableValidation::s_BarycentricSumEpsilon
        && Abs(lhs.bary[1] - rhs.bary[1]) <= DeformableValidation::s_BarycentricSumEpsilon
        && Abs(lhs.bary[2] - rhs.bary[2]) <= DeformableValidation::s_BarycentricSumEpsilon
    ;
}

[[nodiscard]] bool ExactF32(const f32 lhs, const f32 rhs){
    return NWB_MEMCMP(&lhs, &rhs, sizeof(lhs)) == 0;
}

[[nodiscard]] bool ExactFloat3(const Float3U& lhs, const Float3U& rhs){
    return ExactF32(lhs.x, rhs.x)
        && ExactF32(lhs.y, rhs.y)
        && ExactF32(lhs.z, rhs.z)
    ;
}

[[nodiscard]] bool ExactFloat3(const Float4& lhs, const Float4& rhs){
    return ExactF32(lhs.x, rhs.x)
        && ExactF32(lhs.y, rhs.y)
        && ExactF32(lhs.z, rhs.z)
    ;
}

[[nodiscard]] bool ExactSourceSample(const SourceSample& lhs, const SourceSample& rhs){
    return lhs.sourceTri == rhs.sourceTri
        && ExactF32(lhs.bary[0], rhs.bary[0])
        && ExactF32(lhs.bary[1], rhs.bary[1])
        && ExactF32(lhs.bary[2], rhs.bary[2])
    ;
}

[[nodiscard]] bool ExactPosedHit(const DeformablePosedHit& lhs, const DeformablePosedHit& rhs){
    return lhs.entity == rhs.entity
        && lhs.runtimeMesh == rhs.runtimeMesh
        && lhs.editRevision == rhs.editRevision
        && lhs.triangle == rhs.triangle
        && ExactF32(lhs.bary[0], rhs.bary[0])
        && ExactF32(lhs.bary[1], rhs.bary[1])
        && ExactF32(lhs.bary[2], rhs.bary[2])
        && ExactF32(lhs.distance(), rhs.distance())
        && ExactFloat3(lhs.position, rhs.position)
        && ExactFloat3(lhs.normal, rhs.normal)
        && lhs.editMaskFlags == rhs.editMaskFlags
        && ExactSourceSample(lhs.restSample, rhs.restSample)
    ;
}

[[nodiscard]] bool ExactHoleEditParams(const DeformableHoleEditParams& lhs, const DeformableHoleEditParams& rhs){
    return ExactPosedHit(lhs.posedHit, rhs.posedHit)
        && ExactF32(lhs.radius, rhs.radius)
        && ExactF32(lhs.ellipseRatio, rhs.ellipseRatio)
        && ExactF32(lhs.depth, rhs.depth)
    ;
}

[[nodiscard]] bool ValidateHitRestSample(const DeformableRuntimeMeshInstance& instance, const DeformablePosedHit& hit){
    SourceSample resolvedSample{};
    if(!ResolveDeformableRestSurfaceSample(instance, hit.triangle, hit.bary, resolvedSample))
        return false;

    return MatchingSourceSample(hit.restSample, resolvedSample);
}

[[nodiscard]] bool ValidatePosedHitFrame(const DeformablePosedHit& hit){
    const SIMDVector position = LoadFloat(hit.position);
    const SIMDVector normal = LoadFloat(hit.normal);
    const f32 normalLengthSquared = VectorGetX(Vector3LengthSq(normal));
    return IsFinite(hit.distance())
        && hit.distance() >= 0.0f
        && DeformableValidation::FiniteVector(position, 0x7u)
        && DeformableValidation::FiniteVector(normal, 0x7u)
        && Abs(normalLengthSquared - 1.0f) <= DeformableValidation::s_RestFrameUnitLengthSquaredEpsilon
    ;
}

[[nodiscard]] bool ValidateHitIdentity(const DeformableRuntimeMeshInstance& instance, const DeformablePosedHit& hit){
    if(!DeformableValidation::ValidLooseBarycentric(hit.bary.values))
        return false;
    if(!ValidatePosedHitFrame(hit))
        return false;
    if(hit.entity != instance.entity)
        return false;
    if(hit.runtimeMesh != instance.handle)
        return false;
    if(hit.editRevision != instance.editRevision)
        return false;
    if(!ValidateHitRestSample(instance, hit))
        return false;
    if(hit.editMaskFlags != ResolveDeformableTriangleEditMask(instance, hit.triangle))
        return false;
    return true;
}

[[nodiscard]] bool ValidateHoleShapeValues(const f32 radius, const f32 ellipseRatio, const f32 depth){
    return IsFinite(radius)
        && IsFinite(ellipseRatio)
        && IsFinite(depth)
        && IsFinite(radius * ellipseRatio)
        && radius > s_Epsilon
        && (radius * ellipseRatio) > s_Epsilon
        && depth >= 0.0f
    ;
}

[[nodiscard]] bool ValidateHoleShape(const DeformableHoleEditParams& params){
    return ValidateHoleShapeValues(params.radius, params.ellipseRatio, params.depth);
}

[[nodiscard]] bool ValidateParams(const DeformableRuntimeMeshInstance& instance, const DeformableHoleEditParams& params){
    return ValidateHitIdentity(instance, params.posedHit)
        && ValidateHoleShape(params)
        && instance.editRevision != Limit<u32>::s_Max
    ;
}

[[nodiscard]] bool RuntimeMeshUploaded(const DeformableRuntimeMeshInstance& instance){
    return (instance.dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) == 0u;
}

[[nodiscard]] bool ValidateUploadedRuntimePayload(const DeformableRuntimeMeshInstance& instance){
    return ValidateRuntimePayload(instance) && RuntimeMeshUploaded(instance);
}

[[nodiscard]] bool ValidateSurfaceEditSession(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditSession& session)
{
    return session.active
        && session.entity == instance.entity
        && session.runtimeMesh == instance.handle
        && session.editRevision == instance.editRevision
        && ValidateHitIdentity(instance, session.hit)
    ;
}

[[nodiscard]] bool ValidateSurfaceEditSessionParams(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditSession& session,
    const DeformableHoleEditParams& params)
{
    return ValidateSurfaceEditSession(instance, session)
        && ValidateParams(instance, params)
        && ExactPosedHit(session.hit, params.posedHit)
    ;
}

[[nodiscard]] bool ValidatePreviewedSurfaceEditSessionParams(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditSession& session,
    const DeformableHoleEditParams& params)
{
    return ValidateSurfaceEditSessionParams(instance, session, params)
        && session.previewed
        && ExactHoleEditParams(session.previewParams, params)
    ;
}

[[nodiscard]] bool BuildPreviewFrame(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    HoleFrame& outFrame)
{
    u32 hitTriangleIndices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, params.posedHit.triangle, hitTriangleIndices))
        return false;

    f32 hitBary[3] = {};
    if(!DeformableValidation::NormalizeSourceBarycentric(params.posedHit.bary.values, hitBary))
        return false;
    return BuildHoleFrame(instance, hitTriangleIndices, hitBary, outFrame);
}

[[nodiscard]] bool ValidWallVertexSpan(const u32 firstVertex, const u32 vertexCount){
    return firstVertex != Limit<u32>::s_Max
        && vertexCount >= s_MinWallLoopVertexCount
        && vertexCount <= Limit<u32>::s_Max - firstVertex
    ;
}

[[nodiscard]] bool ValidOptionalWallVertexSpan(const u32 firstVertex, const u32 vertexCount){
    if(vertexCount == 0u)
        return firstVertex == Limit<u32>::s_Max;

    return ValidWallVertexSpan(firstVertex, vertexCount);
}

[[nodiscard]] bool ValidWallLoopCutCount(const u32 count){
    return count <= s_MaxWallLoopCutCount;
}

[[nodiscard]] bool ValidSurfaceEditId(const DeformableSurfaceEditId editId){
    return editId != 0u && editId != Limit<DeformableSurfaceEditId>::s_Max;
}

[[nodiscard]] bool ValidHoleEditResult(const DeformableHoleEditResult& result, const bool requireWall){
    if(result.editRevision == 0u || result.removedTriangleCount == 0u)
        return false;
    if(!ValidWallLoopCutCount(result.wallLoopCutCount))
        return false;
    if(!ValidOptionalWallVertexSpan(result.firstWallVertex, result.wallVertexCount))
        return false;
    if(result.wallVertexCount == 0u)
        return !requireWall
            && result.addedVertexCount == 0u
            && result.addedTriangleCount == 0u
            && result.wallLoopCutCount == 0u
        ;
    const u32 wallBandCount = result.wallLoopCutCount + 1u;
    if(result.wallVertexCount > Limit<u32>::s_Max / wallBandCount)
        return false;
    const u32 expectedAddedVertexCount = result.wallVertexCount * wallBandCount;
    if(result.addedVertexCount != expectedAddedVertexCount)
        return false;
    if(result.wallVertexCount > Limit<u32>::s_Max / (2u * wallBandCount))
        return false;
    if(result.addedTriangleCount != result.wallVertexCount * 2u * wallBandCount)
        return false;
    return true;
}

[[nodiscard]] bool RuntimeMeshWallTrianglePairsMatchAt(
    const DeformableRuntimeMeshInstance& instance,
    const usize indexBase,
    const usize firstWallVertex,
    const usize wallVertexCount)
{
    for(usize pairIndex = 0u; pairIndex < wallVertexCount; ++pairIndex){
        const usize nextPairIndex = (pairIndex + 1u) % wallVertexCount;
        const u32 innerA = static_cast<u32>(firstWallVertex + pairIndex);
        const u32 innerB = static_cast<u32>(firstWallVertex + nextPairIndex);
        const usize pairIndexBase = indexBase + (pairIndex * 6u);
        const u32 rimA = instance.indices[pairIndexBase + 0u];
        const u32 rimB = instance.indices[pairIndexBase + 1u];

        if(rimA >= firstWallVertex
            || rimB >= firstWallVertex
            || rimA == rimB
            || instance.indices[pairIndexBase + 2u] != innerB
            || instance.indices[pairIndexBase + 3u] != rimA
            || instance.indices[pairIndexBase + 4u] != innerB
            || instance.indices[pairIndexBase + 5u] != innerA
        )
            return false;
    }
    return true;
}

[[nodiscard]] bool RuntimeMeshHasWallTrianglePairs(
    const DeformableRuntimeMeshInstance& instance,
    const u32 firstWallVertexValue,
    const u32 wallVertexCountValue,
    usize* outIndexBase = nullptr)
{
    if(outIndexBase)
        *outIndexBase = Limit<usize>::s_Max;
    if(!ValidWallVertexSpan(firstWallVertexValue, wallVertexCountValue))
        return false;

    const usize firstWallVertex = static_cast<usize>(firstWallVertexValue);
    const usize wallVertexCount = static_cast<usize>(wallVertexCountValue);
    if(firstWallVertex >= instance.restVertices.size()
        || wallVertexCount > instance.restVertices.size() - firstWallVertex
    )
        return false;

    if(wallVertexCount > Limit<usize>::s_Max / 6u)
        return false;

    const usize wallIndexCount = wallVertexCount * 6u;
    if(wallIndexCount > instance.indices.size())
        return false;

    for(usize indexBase = 0u; indexBase <= instance.indices.size() - wallIndexCount; indexBase += 3u){
        if(!RuntimeMeshWallTrianglePairsMatchAt(instance, indexBase, firstWallVertex, wallVertexCount))
            continue;

        if(outIndexBase)
            *outIndexBase = indexBase;
        return true;
    }
    return false;
}

[[nodiscard]] bool RuntimeMeshHasWallTrianglePairs(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditResult& result)
{
    return ValidHoleEditResult(result, true)
        && RuntimeMeshHasWallTrianglePairs(instance, result.firstWallVertex, result.wallVertexCount)
    ;
}

[[nodiscard]] bool ValidStoredRestFrame(const Float3U& position, const Float3U& normal){
    const SIMDVector positionVector = LoadFloat(position);
    const SIMDVector normalVector = LoadFloat(normal);
    const f32 normalLengthSquared = VectorGetX(Vector3LengthSq(normalVector));
    return DeformableValidation::FiniteVector(positionVector, 0x7u)
        && DeformableValidation::FiniteVector(normalVector, 0x7u)
        && Abs(normalLengthSquared - 1.0f) <= DeformableValidation::s_RestFrameUnitLengthSquaredEpsilon
    ;
}

[[nodiscard]] bool ValidHoleRecord(const DeformableSurfaceHoleEditRecord& record){
    return ValidateHoleShapeValues(record.radius, record.ellipseRatio, record.depth)
        && ValidWallLoopCutCount(record.wallLoopCutCount)
        && (record.wallLoopCutCount == 0u || record.depth > s_Epsilon)
        && record.baseEditRevision != Limit<u32>::s_Max
        && record.restSample.sourceTri != Limit<u32>::s_Max
        && DeformableValidation::ValidSourceBarycentric(record.restSample.bary)
        && ValidStoredRestFrame(record.restPosition, record.restNormal)
    ;
}

[[nodiscard]] bool ValidEditRecord(const DeformableSurfaceEditRecord& record){
    if(!ValidSurfaceEditId(record.editId))
        return false;
    if(record.type != DeformableSurfaceEditRecordType::Hole)
        return false;
    if(!ValidHoleRecord(record.hole))
        return false;
    if(record.result.editRevision != record.hole.baseEditRevision + 1u)
        return false;
    if(record.result.wallLoopCutCount != record.hole.wallLoopCutCount)
        return false;
    if(!ValidHoleEditResult(record.result, true))
        return false;
    return true;
}

[[nodiscard]] bool ValidAccessoryAttachmentValues(
    const DeformableSurfaceEditId anchorEditId,
    const u32 firstWallVertex,
    const u32 wallVertexCount,
    const f32 normalOffset,
    const f32 uniformScale)
{
    return ValidSurfaceEditId(anchorEditId)
        && ValidWallVertexSpan(firstWallVertex, wallVertexCount)
        && IsFinite(normalOffset)
        && normalOffset >= 0.0f
        && IsFinite(uniformScale)
        && uniformScale > 0.0f
    ;
}

[[nodiscard]] bool ValidAccessoryAttachment(const DeformableAccessoryAttachmentComponent& attachment){
    return attachment.targetEntity.valid()
        && attachment.runtimeMesh.valid()
        && ValidAccessoryAttachmentValues(
            attachment.anchorEditId,
            attachment.firstWallVertex,
            attachment.wallVertexCount,
            attachment.normalOffset(),
            attachment.uniformScale()
        )
    ;
}

[[nodiscard]] bool ValidAccessoryRecord(const DeformableAccessoryAttachmentRecord& record){
    return ValidAccessoryAttachmentValues(
            record.anchorEditId,
            record.firstWallVertex,
            record.wallVertexCount,
            record.normalOffset,
            record.uniformScale
        )
        && record.geometry.valid()
        && record.material.valid()
        && (record.geometryVirtualPathText.empty() || Name(record.geometryVirtualPathText.view()) == record.geometry.name())
        && (record.materialVirtualPathText.empty() || Name(record.materialVirtualPathText.view()) == record.material.name())
    ;
}

[[nodiscard]] bool AccessoryRecordHasStableAssetPaths(const DeformableAccessoryAttachmentRecord& record){
    return !record.geometryVirtualPathText.empty()
        && !record.materialVirtualPathText.empty()
        && Name(record.geometryVirtualPathText.view()) == record.geometry.name()
        && Name(record.materialVirtualPathText.view()) == record.material.name()
    ;
}

[[nodiscard]] bool EditRecordMatchesAccessory(
    const DeformableSurfaceEditRecord& record,
    const DeformableAccessoryAttachmentRecord& accessory)
{
    return record.editId == accessory.anchorEditId
        && record.type == DeformableSurfaceEditRecordType::Hole
        && record.result.firstWallVertex == accessory.firstWallVertex
        && record.result.wallVertexCount == accessory.wallVertexCount
    ;
}

[[nodiscard]] const DeformableSurfaceEditRecord* FindEditRecordById(
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId)
{
    if(!ValidSurfaceEditId(editId))
        return nullptr;

    for(const DeformableSurfaceEditRecord& record : state.edits){
        if(record.editId == editId)
            return &record;
    }
    return nullptr;
}

[[nodiscard]] bool ValidSurfaceEditState(const DeformableSurfaceEditState& state){
    u32 expectedBaseEditRevision = 0u;
    for(usize editIndex = 0u; editIndex < state.edits.size(); ++editIndex){
        const DeformableSurfaceEditRecord& record = state.edits[editIndex];
        if(!ValidEditRecord(record))
            return false;
        for(usize previousEditIndex = 0u; previousEditIndex < editIndex; ++previousEditIndex){
            if(state.edits[previousEditIndex].editId == record.editId)
                return false;
        }
        if(record.hole.baseEditRevision != expectedBaseEditRevision)
            return false;
        expectedBaseEditRevision = record.result.editRevision;
    }
    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        const DeformableSurfaceEditRecord* anchorEdit = FindEditRecordById(state, accessory.anchorEditId);
        if(!ValidAccessoryRecord(accessory)
            || !anchorEdit
            || !EditRecordMatchesAccessory(*anchorEdit, accessory)
        )
            return false;
    }
    return true;
}

[[nodiscard]] bool ValidateReplayAccessoryAnchors(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditState& state)
{
    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        const DeformableSurfaceEditRecord* anchorEdit = FindEditRecordById(state, accessory.anchorEditId);
        if(!ValidAccessoryRecord(accessory)
            || !anchorEdit
            || !EditRecordMatchesAccessory(*anchorEdit, accessory)
            || !RuntimeMeshHasWallTrianglePairs(instance, accessory.firstWallVertex, accessory.wallVertexCount)
        )
            return false;
    }
    return true;
}

[[nodiscard]] bool ReplayContextTargetsInstance(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditReplayContext& context)
{
    return !context.targetEntity.valid() || context.targetEntity == instance.entity;
}

[[nodiscard]] bool ComputeTriangleBarycentric(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&indices)[3],
    const SIMDVector point,
    f32 (&outBary)[3])
{
    const SIMDVector a = LoadFloat(instance.restVertices[indices[0]].position);
    const SIMDVector b = LoadFloat(instance.restVertices[indices[1]].position);
    const SIMDVector c = LoadFloat(instance.restVertices[indices[2]].position);
    const SIMDVector v0 = VectorSubtract(b, a);
    const SIMDVector v1 = VectorSubtract(c, a);
    const SIMDVector v2 = VectorSubtract(point, a);

    const f32 d00 = VectorGetX(Vector3Dot(v0, v0));
    const f32 d01 = VectorGetX(Vector3Dot(v0, v1));
    const f32 d11 = VectorGetX(Vector3Dot(v1, v1));
    const f32 d20 = VectorGetX(Vector3Dot(v2, v0));
    const f32 d21 = VectorGetX(Vector3Dot(v2, v1));
    const f32 denominator = (d00 * d11) - (d01 * d01);
    if(!IsFinite(denominator) || Abs(denominator) <= DeformableValidation::s_TriangleAreaLengthSquaredEpsilon)
        return false;

    const f32 invDenominator = 1.0f / denominator;
    const f32 bary1 = ((d11 * d20) - (d01 * d21)) * invDenominator;
    const f32 bary2 = ((d00 * d21) - (d01 * d20)) * invDenominator;
    const f32 bary0 = 1.0f - bary1 - bary2;
    const f32 bary[3] = { bary0, bary1, bary2 };
    if(!DeformableValidation::NormalizeSourceBarycentric(bary, outBary))
        return false;

    const SIMDVector reconstructed = BarycentricPoint(instance, indices, outBary);
    const SIMDVector delta = VectorSubtract(reconstructed, point);
    const f32 distanceSquared = VectorGetX(Vector3LengthSq(delta));
    return IsFinite(distanceSquared) && distanceSquared <= 0.000001f;
}

[[nodiscard]] bool TriangleNormalMatchesStoredNormal(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&indices)[3],
    const Float3U& storedNormal)
{
    const SIMDVector a = LoadFloat(instance.restVertices[indices[0]].position);
    const SIMDVector b = LoadFloat(instance.restVertices[indices[1]].position);
    const SIMDVector c = LoadFloat(instance.restVertices[indices[2]].position);
    const SIMDVector rawNormal = Vector3Cross(VectorSubtract(b, a), VectorSubtract(c, a));
    if(VectorGetX(Vector3LengthSq(rawNormal)) <= Core::Geometry::s_FrameDirectionEpsilon)
        return false;

    const SIMDVector triangleNormal = Core::Geometry::FrameNormalizeDirection(rawNormal, s_SIMDIdentityR2);
    const SIMDVector recordNormal = LoadFloat(storedNormal);
    if(!FiniteVec3(triangleNormal) || !FiniteVec3(recordNormal))
        return false;

    return VectorGetX(Vector3Dot(triangleNormal, recordNormal)) >= 0.9f;
}

[[nodiscard]] bool BuildReplayHoleParams(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditRecord& record,
    const usize triangle,
    DeformableHoleEditParams& outParams)
{
    outParams = DeformableHoleEditParams{};
    if(record.type != DeformableSurfaceEditRecordType::Hole
        || record.hole.baseEditRevision != instance.editRevision
        || !ValidHoleRecord(record.hole)
    )
        return false;

    const SIMDVector restPosition = LoadFloat(record.hole.restPosition);
    if(!FiniteVec3(restPosition))
        return false;

    u32 triangleIndices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, static_cast<u32>(triangle), triangleIndices))
        return false;
    if(!TriangleNormalMatchesStoredNormal(instance, triangleIndices, record.hole.restNormal))
        return false;

    f32 bary[3] = {};
    if(!ComputeTriangleBarycentric(instance, triangleIndices, restPosition, bary))
        return false;

    SourceSample sample{};
    if(!ResolveDeformableRestSurfaceSample(instance, static_cast<u32>(triangle), bary, sample)
        || !MatchingSourceSample(sample, record.hole.restSample)
    )
        return false;

    outParams.posedHit.entity = instance.entity;
    outParams.posedHit.runtimeMesh = instance.handle;
    outParams.posedHit.editRevision = instance.editRevision;
    outParams.posedHit.triangle = static_cast<u32>(triangle);
    outParams.posedHit.bary[0] = bary[0];
    outParams.posedHit.bary[1] = bary[1];
    outParams.posedHit.bary[2] = bary[2];
    outParams.posedHit.setPosition(record.hole.restPosition);
    outParams.posedHit.setNormal(record.hole.restNormal);
    outParams.posedHit.setDistance(0.0f);
    outParams.posedHit.editMaskFlags = ResolveDeformableTriangleEditMask(instance, outParams.posedHit.triangle);
    outParams.posedHit.restSample = record.hole.restSample;
    outParams.radius = record.hole.radius;
    outParams.ellipseRatio = record.hole.ellipseRatio;
    outParams.depth = record.hole.depth;
    return ValidateParams(instance, outParams);
}

[[nodiscard]] bool ValidMoveTargetHoleRecord(const DeformableSurfaceHoleEditRecord& record){
    return record.restSample.sourceTri != Limit<u32>::s_Max
        && DeformableValidation::ValidSourceBarycentric(record.restSample.bary)
        && ValidStoredRestFrame(record.restPosition, record.restNormal)
    ;
}

[[nodiscard]] bool BuildMoveTargetHoleRecord(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePosedHit& hit,
    DeformableSurfaceHoleEditRecord& outRecord)
{
    outRecord = DeformableSurfaceHoleEditRecord{};
    if(!ValidateRuntimePayload(instance) || !ValidateHitIdentity(instance, hit))
        return false;

    DeformableHoleEditParams params;
    params.posedHit = hit;

    HoleFrame frame;
    if(!BuildPreviewFrame(instance, params, frame))
        return false;

    outRecord.restSample = hit.restSample;
    StoreFloat(frame.center, &outRecord.restPosition);
    StoreFloat(frame.normal, &outRecord.restNormal);
    return ValidMoveTargetHoleRecord(outRecord);
}

[[nodiscard]] bool ReplayResultMatchesStoredRecord(
    const DeformableHoleEditResult& result,
    const DeformableSurfaceEditRecord& record)
{
    return result.removedTriangleCount == record.result.removedTriangleCount
        && result.addedVertexCount == record.result.addedVertexCount
        && result.addedTriangleCount == record.result.addedTriangleCount
        && result.editRevision == record.result.editRevision
        && result.firstWallVertex == record.result.firstWallVertex
        && result.wallVertexCount == record.result.wallVertexCount
        && result.wallLoopCutCount == record.result.wallLoopCutCount
    ;
}

[[nodiscard]] bool ReplayResultShapeMatchesStoredRecord(
    const DeformableHoleEditResult& result,
    const DeformableSurfaceEditRecord& record)
{
    return result.removedTriangleCount == record.result.removedTriangleCount
        && result.addedVertexCount == record.result.addedVertexCount
        && result.addedTriangleCount == record.result.addedTriangleCount
        && result.wallVertexCount == record.result.wallVertexCount
        && result.wallLoopCutCount == record.result.wallLoopCutCount
    ;
}

[[nodiscard]] bool HoleResultChangesTopology(const DeformableHoleEditResult& result){
    return result.removedTriangleCount != 0u
        || result.addedTriangleCount != 0u
        || result.addedVertexCount != 0u
    ;
}

[[nodiscard]] bool ValidateReplayAccessories(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditReplayContext& context)
{
    if(state.accessories.empty())
        return ReplayContextTargetsInstance(instance, context);
    if(!context.world || !context.targetEntity.valid() || context.targetEntity != instance.entity)
        return false;

    return ValidateReplayAccessoryAnchors(instance, state);
}

void RestoreReplayAccessories(
    DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditReplayContext& context,
    u32& outRestoredAccessoryCount)
{
    outRestoredAccessoryCount = 0u;
    if(state.accessories.empty() || !context.world)
        return;

    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        auto entity = context.world->createEntity();
        auto& transform = entity.addComponent<Core::Scene::TransformComponent>();
        transform.scale = Float4(accessory.uniformScale, accessory.uniformScale, accessory.uniformScale);

        auto& renderer = entity.addComponent<RendererComponent>();
        renderer.geometry = accessory.geometry;
        renderer.material = accessory.material;
        renderer.visible = false;

        auto& attachment = entity.addComponent<DeformableAccessoryAttachmentComponent>();
        attachment.targetEntity = instance.entity;
        attachment.runtimeMesh = instance.handle;
        attachment.anchorEditId = accessory.anchorEditId;
        attachment.firstWallVertex = accessory.firstWallVertex;
        attachment.wallVertexCount = accessory.wallVertexCount;
        attachment.setNormalOffset(accessory.normalOffset);
        attachment.setUniformScale(accessory.uniformScale);
        ++outRestoredAccessoryCount;
    }
}

[[nodiscard]] bool ValidateReplayAccessoryAssets(
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditReplayContext& context)
{
    if(state.accessories.empty() || !context.assetManager)
        return true;

    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        UniquePtr<Core::Assets::IAsset> geometryAsset;
        if(!context.assetManager->loadSync(Geometry::AssetTypeName(), accessory.geometry.name(), geometryAsset)
            || !geometryAsset
            || geometryAsset->assetType() != Geometry::AssetTypeName()
        )
            return false;

        UniquePtr<Core::Assets::IAsset> materialAsset;
        if(!context.assetManager->loadSync(Material::AssetTypeName(), accessory.material.name(), materialAsset)
            || !materialAsset
            || materialAsset->assetType() != Material::AssetTypeName()
        )
            return false;
    }
    return true;
}

[[nodiscard]] bool AppendStringTablePath(Core::Assets::AssetBytes& stringTable, const CompactString& pathText, u32& outOffset){
    outOffset = Limit<u32>::s_Max;
    if(pathText.empty() || stringTable.size() >= Limit<u32>::s_Max)
        return false;

    const usize beginOffset = stringTable.size();
    const usize byteCount = pathText.size() + 1u;
    if(beginOffset > Limit<usize>::s_Max - byteCount)
        return false;
    if(byteCount > static_cast<usize>(Limit<u32>::s_Max) - beginOffset)
        return false;

    outOffset = static_cast<u32>(beginOffset);
    stringTable.resize(beginOffset + byteCount);
    NWB_MEMCPY(stringTable.data() + beginOffset, pathText.size(), pathText.data(), pathText.size());
    stringTable[beginOffset + pathText.size()] = 0u;
    return true;
}

[[nodiscard]] bool ReadStringTablePath(
    const Core::Assets::AssetBytes& binary,
    const usize stringTableOffset,
    const usize stringTableByteCount,
    const u32 pathOffset,
    CompactString& outPath)
{
    outPath.clear();
    if(pathOffset == Limit<u32>::s_Max || static_cast<usize>(pathOffset) >= stringTableByteCount)
        return false;

    const usize absoluteOffset = stringTableOffset + static_cast<usize>(pathOffset);
    const usize remainingBytes = stringTableByteCount - static_cast<usize>(pathOffset);
    usize textLength = 0u;
    while(textLength < remainingBytes && binary[absoluteOffset + textLength] != 0u)
        ++textLength;

    if(textLength == 0u || textLength >= remainingBytes)
        return false;

    return outPath.assign(AStringView(reinterpret_cast<const char*>(binary.data() + absoluteOffset), textLength));
}

[[nodiscard]] bool BuildAccessoryBinaryRecordV4(
    const DeformableAccessoryAttachmentRecord& record,
    SurfaceEditAccessoryRecordBinaryV4& outRecord,
    Core::Assets::AssetBytes& stringTable)
{
    outRecord = SurfaceEditAccessoryRecordBinaryV4{};
    if(!ValidAccessoryRecord(record) || !AccessoryRecordHasStableAssetPaths(record))
        return false;

    outRecord.anchorEditId = record.anchorEditId;
    outRecord.firstWallVertex = record.firstWallVertex;
    outRecord.wallVertexCount = record.wallVertexCount;
    outRecord.normalOffset = record.normalOffset;
    outRecord.uniformScale = record.uniformScale;
    return AppendStringTablePath(stringTable, record.geometryVirtualPathText, outRecord.geometryPathOffset)
        && AppendStringTablePath(stringTable, record.materialVirtualPathText, outRecord.materialPathOffset)
    ;
}

[[nodiscard]] bool BuildAccessoryRecordV4(
    const SurfaceEditAccessoryRecordBinaryV4& binary,
    const Core::Assets::AssetBytes& rawBinary,
    const usize stringTableOffset,
    const usize stringTableByteCount,
    DeformableAccessoryAttachmentRecord& outRecord)
{
    outRecord = DeformableAccessoryAttachmentRecord{};
    outRecord.anchorEditId = binary.anchorEditId;
    outRecord.firstWallVertex = binary.firstWallVertex;
    outRecord.wallVertexCount = binary.wallVertexCount;
    outRecord.normalOffset = binary.normalOffset;
    outRecord.uniformScale = binary.uniformScale;
    if(!ReadStringTablePath(
            rawBinary,
            stringTableOffset,
            stringTableByteCount,
            binary.geometryPathOffset,
            outRecord.geometryVirtualPathText
        )
        || !ReadStringTablePath(
            rawBinary,
            stringTableOffset,
            stringTableByteCount,
            binary.materialPathOffset,
            outRecord.materialVirtualPathText
        )
    )
        return false;

    outRecord.geometry.virtualPath = Name(outRecord.geometryVirtualPathText.view());
    outRecord.material.virtualPath = Name(outRecord.materialVirtualPathText.view());
    return ValidAccessoryRecord(outRecord) && AccessoryRecordHasStableAssetPaths(outRecord);
}

[[nodiscard]] bool ComputeSurfaceEditStateBinarySizeV4(
    const u64 editCount,
    const u64 accessoryCount,
    const u64 stringTableByteCount,
    usize& outSize)
{
    outSize = sizeof(SurfaceEditStateHeaderV4);
    if(editCount > static_cast<u64>(Limit<usize>::s_Max / sizeof(DeformableSurfaceEditRecord)))
        return false;
    if(accessoryCount > static_cast<u64>(Limit<usize>::s_Max / sizeof(SurfaceEditAccessoryRecordBinaryV4)))
        return false;
    if(stringTableByteCount > static_cast<u64>(Limit<u32>::s_Max))
        return false;

    const usize editBytes = static_cast<usize>(editCount) * sizeof(DeformableSurfaceEditRecord);
    const usize accessoryBytes = static_cast<usize>(accessoryCount) * sizeof(SurfaceEditAccessoryRecordBinaryV4);
    const usize stringTableBytes = static_cast<usize>(stringTableByteCount);
    if(editBytes > Limit<usize>::s_Max - outSize)
        return false;
    outSize += editBytes;
    if(accessoryBytes > Limit<usize>::s_Max - outSize)
        return false;
    outSize += accessoryBytes;
    if(stringTableBytes > Limit<usize>::s_Max - outSize)
        return false;
    outSize += stringTableBytes;
    return true;
}

[[nodiscard]] SIMDVector RotationFromPositiveZToNormalVector(const SIMDVector rawNormalVector){
    if(!DeformableValidation::FiniteVector(rawNormalVector, 0x7u))
        return s_SIMDIdentityR3;

    const SIMDVector normalVector = Core::Geometry::FrameNormalizeDirection(rawNormalVector, s_SIMDIdentityR2);
    if(!DeformableValidation::FiniteVector(normalVector, 0x7u))
        return s_SIMDIdentityR3;

    f32 dot = VectorGetZ(normalVector);
    if(dot > 1.0f)
        dot = 1.0f;
    if(dot < -1.0f)
        dot = -1.0f;

    if(dot > 0.999f)
        return s_SIMDIdentityR3;
    if(dot < -0.999f)
        return s_SIMDIdentityR0;

    const SIMDVector axis = VectorMultiply(VectorSwizzle<1, 0, 2, 3>(normalVector), VectorSet(-1.0f, 1.0f, 0.0f, 0.0f));
    const f32 scale = VectorGetX(VectorSqrt(VectorReplicate((1.0f + dot) * 2.0f)));
    if(!IsFinite(scale) || scale <= Core::Geometry::s_FrameDirectionEpsilon)
        return s_SIMDIdentityR3;

    SIMDVector rotationVector = VectorScale(axis, 1.0f / scale);
    return VectorSetW(rotationVector, scale * 0.5f);
}

[[nodiscard]] SIMDVector NormalizeRotationQuaternionVector(
    const SIMDVector rotationVector,
    const SIMDVector fallbackNormalVector)
{
    const f32 lengthSquared = VectorGetX(QuaternionLengthSq(rotationVector));
    if(!IsFinite(lengthSquared) || lengthSquared <= Core::Geometry::s_FrameDirectionEpsilon)
        return RotationFromPositiveZToNormalVector(fallbackNormalVector);

    return QuaternionNormalize(rotationVector);
}

[[nodiscard]] SIMDVector RotationFromFrame(const SIMDVector rawTangentVector, const SIMDVector rawNormalVector){
    if(!DeformableValidation::FiniteVector(rawNormalVector, 0x7u))
        return s_SIMDIdentityR3;

    const SIMDVector normalVector = Core::Geometry::FrameNormalizeDirection(rawNormalVector, s_SIMDIdentityR2);
    if(!DeformableValidation::FiniteVector(normalVector, 0x7u))
        return s_SIMDIdentityR3;

    const SIMDVector fallbackTangent = Core::Geometry::FrameFallbackTangent(normalVector);
    SIMDVector tangentVector{};
    SIMDVector bitangentVector{};
    ResolveTangentBitangentVectors(
        normalVector,
        DeformableValidation::FiniteVector(rawTangentVector, 0x7u) ? rawTangentVector : fallbackTangent,
        fallbackTangent,
        tangentVector,
        bitangentVector
    );

    SIMDMatrix basisAsRows{};
    basisAsRows.v[0] = VectorAndInt(tangentVector, s_SIMDMask3);
    basisAsRows.v[1] = VectorAndInt(bitangentVector, s_SIMDMask3);
    basisAsRows.v[2] = VectorAndInt(normalVector, s_SIMDMask3);
    basisAsRows.v[3] = s_SIMDIdentityR3;

    return NormalizeRotationQuaternionVector(
        QuaternionRotationMatrix(MatrixTranspose(basisAsRows)),
        normalVector
    );
}

[[nodiscard]] bool BuildMorphDeltaLookup(
    const DeformableMorph& morph,
    const usize sourceDeltaCount,
    MorphDeltaLookup& outLookup)
{
    outLookup.reserve(sourceDeltaCount);
    for(usize deltaIndex = 0u; deltaIndex < sourceDeltaCount; ++deltaIndex){
        if(!outLookup.emplace(morph.deltas[deltaIndex].vertexId, deltaIndex).second)
            return false;
    }
    return true;
}

[[nodiscard]] const DeformableMorphDelta* FindMorphDelta(
    const DeformableMorph& morph,
    const MorphDeltaLookup& lookup,
    const u32 vertex)
{
    const auto found = lookup.find(vertex);
    if(found == lookup.end())
        return nullptr;

    const usize deltaIndex = found.value();
    if(deltaIndex >= morph.deltas.size())
        return nullptr;
    return &morph.deltas[deltaIndex];
}

[[nodiscard]] Core::Geometry::AttributeTransferMorphDelta ToAttributeTransferMorphDelta(const DeformableMorphDelta& source){
    Core::Geometry::AttributeTransferMorphDelta delta;
    delta.vertexId = source.vertexId;
    delta.deltaPosition = source.deltaPosition;
    delta.deltaNormal = source.deltaNormal;
    delta.deltaTangent = source.deltaTangent;
    return delta;
}

[[nodiscard]] DeformableMorphDelta ToDeformableMorphDelta(const Core::Geometry::AttributeTransferMorphDelta& source){
    DeformableMorphDelta delta;
    delta.vertexId = source.vertexId;
    delta.deltaPosition = source.deltaPosition;
    delta.deltaNormal = source.deltaNormal;
    delta.deltaTangent = source.deltaTangent;
    return delta;
}

template<usize sourceCount>
[[nodiscard]] bool AppendBlendedMorphDelta(
    Vector<DeformableMorphDelta>& outDeltas,
    const DeformableMorph& sourceMorph,
    const MorphDeltaLookup& lookup,
    const u32 (&sourceVertices)[sourceCount],
    const f32 (&sourceWeights)[sourceCount],
    const u32 outputVertex)
{
    static_assert(sourceCount > 0u, "morph transfer requires source samples");
    Core::Geometry::AttributeTransferMorphDelta sourceDeltas[sourceCount] = {};
    Core::Geometry::AttributeTransferMorphBlendSource sources[sourceCount] = {};
    usize activeSourceCount = 0u;
    for(usize sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex){
        const f32 weight = sourceWeights[sourceIndex];
        if(!IsFinite(weight) || weight < 0.0f)
            return false;
        if(!DeformableValidation::ActiveWeight(weight))
            continue;

        const DeformableMorphDelta* sourceDelta = FindMorphDelta(
            sourceMorph,
            lookup,
            sourceVertices[sourceIndex]
        );
        if(!sourceDelta)
            continue;

        sourceDeltas[activeSourceCount] = ToAttributeTransferMorphDelta(*sourceDelta);
        sources[activeSourceCount].delta = &sourceDeltas[activeSourceCount];
        sources[activeSourceCount].weight = weight;
        ++activeSourceCount;
    }

    if(activeSourceCount == 0u)
        return true;

    Core::Geometry::AttributeTransferMorphDelta blendedDelta;
    bool hasBlendedDelta = false;
    if(!Core::Geometry::BlendMorphDelta(sources, activeSourceCount, outputVertex, blendedDelta, hasBlendedDelta))
        return false;
    if(!hasBlendedDelta)
        return true;

    outDeltas.push_back(ToDeformableMorphDelta(blendedDelta));
    return true;
}

template<typename EdgeVector, typename VertexVector>
[[nodiscard]] bool TransferWallMorphDeltas(
    Vector<DeformableMorph>& morphs,
    const EdgeVector& orderedBoundaryEdges,
    const VertexVector& innerVertices)
{
    if(orderedBoundaryEdges.size() != innerVertices.size())
        return false;
    if(innerVertices.empty())
        return true;

    const usize maxAddedDeltaCount = innerVertices.size();
    Core::Alloc::ScratchArena<> scratchArena;
    for(DeformableMorph& morph : morphs){
        const usize sourceDeltaCount = morph.deltas.size();
        if(morph.deltas.size() > static_cast<usize>(Limit<u32>::s_Max)
            || maxAddedDeltaCount > static_cast<usize>(Limit<u32>::s_Max) - morph.deltas.size()
        )
            return false;
        if(sourceDeltaCount == 0u)
            continue;

        MorphDeltaLookup lookup(
            0,
            Hasher<u32>(),
            EqualTo<u32>(),
            Core::Alloc::ScratchAllocator<Pair<const u32, usize>>(scratchArena)
        );
        if(!BuildMorphDeltaLookup(morph, sourceDeltaCount, lookup))
            return false;

        morph.deltas.reserve(morph.deltas.size() + maxAddedDeltaCount);
        for(usize edgeIndex = 0u; edgeIndex < orderedBoundaryEdges.size(); ++edgeIndex){
            const usize previousEdgeIndex = edgeIndex == 0u ? orderedBoundaryEdges.size() - 1u : edgeIndex - 1u;
            const EdgeRecord& edge = orderedBoundaryEdges[edgeIndex];
            const u32 innerSourceVertices[3] = {
                orderedBoundaryEdges[previousEdgeIndex].a,
                edge.a,
                edge.b,
            };

            if(!AppendBlendedMorphDelta(
                    morph.deltas,
                    morph,
                    lookup,
                    innerSourceVertices,
                    s_WallInnerInpaintWeights,
                    innerVertices[edgeIndex]
                )
            )
                return false;
        }
    }
    return true;
}

[[nodiscard]] Core::Geometry::AttributeTransferSkinInfluence4 ToAttributeTransferSkinInfluence(const SkinInfluence4& source){
    Core::Geometry::AttributeTransferSkinInfluence4 influence;
    for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        influence.joint[influenceIndex] = source.joint[influenceIndex];
        influence.weight[influenceIndex] = source.weight[influenceIndex];
    }
    return influence;
}

[[nodiscard]] SkinInfluence4 ToDeformableSkinInfluence(const Core::Geometry::AttributeTransferSkinInfluence4& source){
    SkinInfluence4 influence;
    for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        influence.joint[influenceIndex] = source.joint[influenceIndex];
        influence.weight[influenceIndex] = source.weight[influenceIndex];
    }
    return influence;
}

template<usize sourceCount>
[[nodiscard]] bool BuildBlendedSkinInfluence(
    const Vector<SkinInfluence4>& skin,
    const u32 (&sourceVertices)[sourceCount],
    const f32 (&sourceWeights)[sourceCount],
    SkinInfluence4& outSkin)
{
    static_assert(sourceCount > 0u, "skin transfer requires source samples");
    outSkin = SkinInfluence4{};
    if(skin.empty())
        return false;

    Core::Geometry::AttributeTransferSkinBlendSource sources[sourceCount] = {};
    usize activeSourceCount = 0u;
    for(usize sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex){
        const f32 sourceWeight = sourceWeights[sourceIndex];
        if(!IsFinite(sourceWeight) || sourceWeight < 0.0f)
            return false;
        if(!DeformableValidation::ActiveWeight(sourceWeight))
            continue;

        const u32 vertex = sourceVertices[sourceIndex];
        if(vertex >= skin.size())
            return false;

        const SkinInfluence4& sourceSkin = skin[vertex];
        if(!DeformableValidation::ValidSkinInfluence(sourceSkin))
            return false;

        sources[activeSourceCount].influence = ToAttributeTransferSkinInfluence(sourceSkin);
        sources[activeSourceCount].weight = sourceWeight;
        ++activeSourceCount;
    }

    Core::Geometry::AttributeTransferSkinInfluence4 blendedSkin;
    if(!Core::Geometry::BlendSkinInfluence4(sources, activeSourceCount, blendedSkin))
        return false;

    outSkin = ToDeformableSkinInfluence(blendedSkin);
    return DeformableValidation::ValidSkinInfluence(outSkin);
}

template<usize sourceCount>
[[nodiscard]] bool BuildBlendedVertexColor(
    const Vector<DeformableVertexRest>& vertices,
    const u32 (&sourceVertices)[sourceCount],
    const f32 (&sourceWeights)[sourceCount],
    Float4U& outColor)
{
    static_assert(sourceCount > 0u, "color transfer requires source samples");
    outColor = Float4U(0.0f, 0.0f, 0.0f, 0.0f);

    Core::Geometry::AttributeTransferFloat4BlendSource sources[sourceCount] = {};
    usize activeSourceCount = 0u;
    for(usize sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex){
        const f32 sourceWeight = sourceWeights[sourceIndex];
        if(!IsFinite(sourceWeight) || sourceWeight < 0.0f)
            return false;
        if(!DeformableValidation::ActiveWeight(sourceWeight))
            continue;

        const u32 vertex = sourceVertices[sourceIndex];
        if(vertex >= vertices.size())
            return false;

        const Float4U& sourceColor = vertices[vertex].color0;
        sources[activeSourceCount].value = sourceColor;
        sources[activeSourceCount].weight = sourceWeight;
        ++activeSourceCount;
    }

    return Core::Geometry::BlendFloat4(sources, activeSourceCount, outColor);
}

[[nodiscard]] bool AppendWallVertex(
    Vector<DeformableVertexRest>& vertices,
    Vector<SkinInfluence4>& skin,
    Vector<SourceSample>& sourceSamples,
    const u32 sourceVertex,
    const SkinInfluence4* wallSkin,
    const SourceSample& wallSourceSample,
    const Float4U& wallColor,
    const SIMDVector position,
    const SIMDVector normal,
    const SIMDVector tangent,
    const f32 uvU,
    const f32 uvV,
    u32& outVertex)
{
    if(sourceVertex >= vertices.size() || vertices.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(!skin.empty() && sourceVertex >= skin.size())
        return false;
    if(!skin.empty() && (!wallSkin || !DeformableValidation::ValidSkinInfluence(*wallSkin)))
        return false;
    if(!sourceSamples.empty() && sourceVertex >= sourceSamples.size())
        return false;

    DeformableVertexRest wallVertex = vertices[sourceVertex];
    StoreFloat(position, &wallVertex.position);
    StoreFloat(normal, &wallVertex.normal);
    StoreFloat(VectorSetW(tangent, Core::Geometry::FrameTangentHandedness(VectorGetW(tangent), 1.0f)), &wallVertex.tangent);
    wallVertex.uv0 = Float2U(uvU, uvV);
    wallVertex.color0 = wallColor;
    if(!DeformableValidation::ValidRestVertexFrame(wallVertex))
        return false;

    outVertex = static_cast<u32>(vertices.size());
    vertices.push_back(wallVertex);
    if(!skin.empty())
        skin.push_back(*wallSkin);
    if(!sourceSamples.empty())
        sourceSamples.push_back(wallSourceSample);
    return true;
}

[[nodiscard]] bool RebuildRuntimeMeshTangentFrames(Vector<DeformableVertexRest>& vertices, const Vector<u32>& indices){
    Vector<Core::Geometry::TangentFrameRebuildVertex> rebuildVertices;
    rebuildVertices.reserve(vertices.size());
    for(const DeformableVertexRest& vertex : vertices){
        Core::Geometry::TangentFrameRebuildVertex rebuildVertex;
        rebuildVertex.position = vertex.position;
        rebuildVertex.uv0 = vertex.uv0;
        rebuildVertex.normal = vertex.normal;
        rebuildVertex.tangent = vertex.tangent;
        rebuildVertices.push_back(rebuildVertex);
    }

    if(!Core::Geometry::RebuildTangentFrames(rebuildVertices, indices))
        return false;

    for(usize vertexIndex = 0u; vertexIndex < vertices.size(); ++vertexIndex){
        DeformableVertexRest& vertex = vertices[vertexIndex];
        vertex.normal = rebuildVertices[vertexIndex].normal;
        vertex.tangent = rebuildVertices[vertexIndex].tangent;
        if(!DeformableValidation::ValidRestVertexFrame(vertex))
            return false;
    }
    return true;
}

[[nodiscard]] bool TriangleInsideFootprint(
    const DeformableRuntimeMeshInstance& instance,
    const HoleFrame& frame,
    const f32 radiusX,
    const f32 radiusY,
    const u32 (&triangleIndices)[3])
{
    const SIMDVector centroid = TriangleCentroid(instance, triangleIndices);
    const SIMDVector offset = VectorSubtract(centroid, frame.center);
    const f32 x = VectorGetX(Vector3Dot(offset, frame.tangent)) / radiusX;
    const f32 y = VectorGetX(Vector3Dot(offset, frame.bitangent)) / radiusY;
    return ((x * x) + (y * y)) <= 1.0f;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





bool BeginSurfaceEdit(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePosedHit& hit,
    DeformableSurfaceEditSession& outSession)
{
    outSession = DeformableSurfaceEditSession{};
    if(!__hidden_deformable_surface_edit::ValidateUploadedRuntimePayload(instance))
        return false;
    if(!__hidden_deformable_surface_edit::ValidateHitIdentity(instance, hit))
        return false;

    outSession.entity = instance.entity;
    outSession.runtimeMesh = instance.handle;
    outSession.editRevision = instance.editRevision;
    outSession.hit = hit;
    outSession.active = true;
    return true;
}

bool PreviewHole(
    const DeformableRuntimeMeshInstance& instance,
    DeformableSurfaceEditSession& session,
    const DeformableHoleEditParams& params,
    DeformableHolePreview& outPreview)
{
    outPreview = DeformableHolePreview{};
    session.previewParams = DeformableHoleEditParams{};
    session.previewed = false;
    if(!__hidden_deformable_surface_edit::ValidateUploadedRuntimePayload(instance)
        || !__hidden_deformable_surface_edit::ValidateSurfaceEditSessionParams(instance, session, params)
    )
        return false;

    __hidden_deformable_surface_edit::HoleFrame frame;
    if(!__hidden_deformable_surface_edit::BuildPreviewFrame(instance, params, frame))
        return false;

    StoreFloat(VectorSetW(frame.center, 1.0f), &outPreview.center);
    StoreFloat(VectorSetW(frame.normal, 0.0f), &outPreview.normal);
    StoreFloat(VectorSetW(frame.tangent, 0.0f), &outPreview.tangent);
    StoreFloat(VectorSetW(frame.bitangent, 0.0f), &outPreview.bitangent);
    outPreview.radius = params.radius;
    outPreview.ellipseRatio = params.ellipseRatio;
    outPreview.depth = params.depth;
    outPreview.editRevision = instance.editRevision;
    outPreview.editMaskFlags = params.posedHit.editMaskFlags;
    outPreview.editPermission = __hidden_deformable_surface_edit::ResolveSurfaceEditPermission(outPreview.editMaskFlags);
    outPreview.valid = true;
    session.previewParams = params;
    session.previewed = true;
    return true;
}

bool CommitHole(
    DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditSession& session,
    const DeformableHoleEditParams& params,
    DeformableHoleEditResult* outResult,
    DeformableSurfaceEditRecord* outRecord)
{
    if(outResult)
        *outResult = DeformableHoleEditResult{};
    if(outRecord)
        *outRecord = DeformableSurfaceEditRecord{};
    if(!__hidden_deformable_surface_edit::ValidateUploadedRuntimePayload(instance)
        || !__hidden_deformable_surface_edit::ValidatePreviewedSurfaceEditSessionParams(instance, session, params)
    )
        return false;

    __hidden_deformable_surface_edit::HoleFrame recordFrame;
    if(outRecord && !__hidden_deformable_surface_edit::BuildPreviewFrame(instance, params, recordFrame))
        return false;

    DeformableHoleEditResult result;
    if(!CommitDeformableRestSpaceHole(instance, params, &result))
        return false;

    if(outResult)
        *outResult = result;
    if(outRecord){
        outRecord->editId = result.editRevision;
        outRecord->type = DeformableSurfaceEditRecordType::Hole;
        outRecord->hole.restSample = params.posedHit.restSample;
        StoreFloat(recordFrame.center, &outRecord->hole.restPosition);
        StoreFloat(recordFrame.normal, &outRecord->hole.restNormal);
        outRecord->hole.baseEditRevision = session.editRevision;
        outRecord->hole.radius = params.radius;
        outRecord->hole.ellipseRatio = params.ellipseRatio;
        outRecord->hole.depth = params.depth;
        outRecord->hole.wallLoopCutCount = result.wallLoopCutCount;
        outRecord->result = result;
    }
    return true;
}

bool AttachAccessory(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditResult& holeResult,
    const f32 normalOffset,
    const f32 uniformScale,
    DeformableAccessoryAttachmentComponent& outAttachment)
{
    outAttachment = DeformableAccessoryAttachmentComponent{};
    if(!__hidden_deformable_surface_edit::ValidateUploadedRuntimePayload(instance)
        || holeResult.editRevision > instance.editRevision
        || !__hidden_deformable_surface_edit::ValidAccessoryAttachmentValues(
            holeResult.editRevision,
            holeResult.firstWallVertex,
            holeResult.wallVertexCount,
            normalOffset,
            uniformScale
        )
        || !__hidden_deformable_surface_edit::RuntimeMeshHasWallTrianglePairs(instance, holeResult)
    )
        return false;

    outAttachment.targetEntity = instance.entity;
    outAttachment.runtimeMesh = instance.handle;
    outAttachment.anchorEditId = holeResult.editRevision;
    outAttachment.firstWallVertex = holeResult.firstWallVertex;
    outAttachment.wallVertexCount = holeResult.wallVertexCount;
    outAttachment.setNormalOffset(normalOffset);
    outAttachment.setUniformScale(uniformScale);
    return true;
}

bool ResolveAccessoryAttachmentTransform(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    const DeformableAccessoryAttachmentComponent& attachment,
    Core::Scene::TransformComponent& outTransform)
{
    if(!__hidden_deformable_surface_edit::ValidAccessoryAttachment(attachment)
        || attachment.targetEntity != instance.entity
        || attachment.runtimeMesh != instance.handle
        || !__hidden_deformable_surface_edit::ValidateUploadedRuntimePayload(instance)
    )
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<DeformableVertexRest, Core::Alloc::ScratchAllocator<DeformableVertexRest>> posedVertices{
        Core::Alloc::ScratchAllocator<DeformableVertexRest>(scratchArena)
    };
    if(!BuildDeformablePickingVertices(instance, inputs, posedVertices))
        return false;

    const usize firstWallVertex = static_cast<usize>(attachment.firstWallVertex);
    const usize wallVertexCount = static_cast<usize>(attachment.wallVertexCount);
    if(firstWallVertex >= posedVertices.size() || wallVertexCount > posedVertices.size() - firstWallVertex)
        return false;
    if(wallVertexCount > Limit<usize>::s_Max / 6u)
        return false;

    usize wallIndexBase = Limit<usize>::s_Max;
    if(!__hidden_deformable_surface_edit::RuntimeMeshHasWallTrianglePairs(
            instance,
            attachment.firstWallVertex,
            attachment.wallVertexCount,
            &wallIndexBase
        )
    )
        return false;

    usize outerWallIndexBase = wallIndexBase;
    usize tracedFirstWallVertex = firstWallVertex;
    while(tracedFirstWallVertex >= wallVertexCount){
        const usize candidateFirstWallVertex = tracedFirstWallVertex - wallVertexCount;
        if(candidateFirstWallVertex > static_cast<usize>(Limit<u32>::s_Max))
            break;

        usize candidateWallIndexBase = Limit<usize>::s_Max;
        if(!__hidden_deformable_surface_edit::RuntimeMeshHasWallTrianglePairs(
                instance,
                static_cast<u32>(candidateFirstWallVertex),
                attachment.wallVertexCount,
                &candidateWallIndexBase
            )
        )
            break;

        tracedFirstWallVertex = candidateFirstWallVertex;
        outerWallIndexBase = candidateWallIndexBase;
    }

    SIMDVector rimCenter = VectorZero();
    SIMDVector innerCenter = VectorZero();
    SIMDVector firstRimPosition = VectorZero();
    for(usize pairIndex = 0u; pairIndex < wallVertexCount; ++pairIndex){
        const usize outerPairIndexBase = outerWallIndexBase + (pairIndex * 6u);
        const usize innerPairIndexBase = wallIndexBase + (pairIndex * 6u);
        const usize innerVertexIndex = firstWallVertex + pairIndex;
        if(outerPairIndexBase + 5u >= instance.indices.size()
            || innerPairIndexBase + 5u >= instance.indices.size()
        )
            return false;

        const usize rimVertexIndex = static_cast<usize>(instance.indices[outerPairIndexBase + 0u]);
        if(rimVertexIndex >= posedVertices.size() || innerVertexIndex >= posedVertices.size())
            return false;

        const SIMDVector rimPosition = LoadFloat(posedVertices[rimVertexIndex].position);
        if(pairIndex == 0u)
            firstRimPosition = rimPosition;
        rimCenter = VectorAdd(rimCenter, rimPosition);
        innerCenter = VectorAdd(innerCenter, LoadFloat(posedVertices[innerVertexIndex].position));
    }

    const f32 invWallVertexCount = 1.0f / static_cast<f32>(wallVertexCount);
    rimCenter = VectorScale(rimCenter, invWallVertexCount);
    innerCenter = VectorScale(innerCenter, invWallVertexCount);
    SIMDVector normal = Core::Geometry::FrameNormalizeDirection(
        VectorSubtract(rimCenter, innerCenter),
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
    );
    if(!__hidden_deformable_surface_edit::FiniteVec3(normal)
        || VectorGetX(Vector3LengthSq(normal)) <= Core::Geometry::s_FrameDirectionEpsilon
    )
        return false;

    const SIMDVector accessoryPosition = VectorMultiplyAdd(
        normal,
        VectorReplicate(attachment.normalOffset()),
        rimCenter
    );
    if(!__hidden_deformable_surface_edit::FiniteVec3(accessoryPosition))
        return false;

    const SIMDVector tangent = Core::Geometry::FrameResolveTangent(
        normal,
        VectorSubtract(firstRimPosition, rimCenter),
        Core::Geometry::FrameFallbackTangent(normal)
    );

    StoreFloat(VectorSetW(accessoryPosition, 0.0f), &outTransform.position);
    StoreFloat(
        __hidden_deformable_surface_edit::RotationFromFrame(tangent, normal),
        &outTransform.rotation
    );
    const f32 uniformScale = attachment.uniformScale();
    outTransform.scale = Float4(uniformScale, uniformScale, uniformScale);
    return true;
}

bool SerializeSurfaceEditState(const DeformableSurfaceEditState& state, Core::Assets::AssetBytes& outBinary){
    outBinary.clear();
    if(!__hidden_deformable_surface_edit::ValidSurfaceEditState(state))
        return false;

    using AccessoryRecord = __hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinaryV4;
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<AccessoryRecord, Core::Alloc::ScratchAllocator<AccessoryRecord>> accessoryRecords{
        Core::Alloc::ScratchAllocator<AccessoryRecord>(scratchArena)
    };
    accessoryRecords.reserve(state.accessories.size());
    Core::Assets::AssetBytes stringTable;
    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        __hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinaryV4 binaryRecord;
        if(!__hidden_deformable_surface_edit::BuildAccessoryBinaryRecordV4(accessory, binaryRecord, stringTable)){
            outBinary.clear();
            return false;
        }
        accessoryRecords.push_back(binaryRecord);
    }

    usize binarySize = 0u;
    if(!__hidden_deformable_surface_edit::ComputeSurfaceEditStateBinarySizeV4(
            static_cast<u64>(state.edits.size()),
            static_cast<u64>(state.accessories.size()),
            static_cast<u64>(stringTable.size()),
            binarySize
        )
    )
        return false;

    outBinary.reserve(binarySize);
    __hidden_deformable_surface_edit::SurfaceEditStateHeaderV4 header;
    header.editCount = static_cast<u64>(state.edits.size());
    header.accessoryCount = static_cast<u64>(state.accessories.size());
    header.stringTableByteCount = static_cast<u64>(stringTable.size());
    AppendPOD(outBinary, header);
    for(const DeformableSurfaceEditRecord& record : state.edits)
        AppendPOD(outBinary, record);
    for(const __hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinaryV4& binaryRecord : accessoryRecords)
        AppendPOD(outBinary, binaryRecord);
    outBinary.insert(outBinary.end(), stringTable.begin(), stringTable.end());
    return outBinary.size() == binarySize;
}

bool DeserializeSurfaceEditState(const Core::Assets::AssetBytes& binary, DeformableSurfaceEditState& outState){
    outState = DeformableSurfaceEditState{};
    usize cursor = 0;
    u32 magic = 0u;
    u32 version = 0u;
    if(!ReadPOD(binary, cursor, magic)
        || !ReadPOD(binary, cursor, version)
        || magic != __hidden_deformable_surface_edit::s_SurfaceEditStateMagic
    )
        return false;

    u64 editCount = 0u;
    u64 accessoryCount = 0u;
    u64 stringTableByteCount = 0u;
    usize expectedSize = 0u;
    if(version != __hidden_deformable_surface_edit::s_SurfaceEditStateVersion)
        return false;
    if(!ReadPOD(binary, cursor, editCount)
        || !ReadPOD(binary, cursor, accessoryCount)
        || !ReadPOD(binary, cursor, stringTableByteCount)
        || !__hidden_deformable_surface_edit::ComputeSurfaceEditStateBinarySizeV4(
            editCount,
            accessoryCount,
            stringTableByteCount,
            expectedSize
        )
        || expectedSize != binary.size()
    )
        return false;

    outState.edits.resize(static_cast<usize>(editCount));
    for(DeformableSurfaceEditRecord& record : outState.edits){
        if(!ReadPOD(binary, cursor, record) || !__hidden_deformable_surface_edit::ValidEditRecord(record)){
            outState = DeformableSurfaceEditState{};
            return false;
        }
    }

    outState.accessories.resize(static_cast<usize>(accessoryCount));
    using AccessoryRecord = __hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinaryV4;
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<AccessoryRecord, Core::Alloc::ScratchAllocator<AccessoryRecord>> accessoryRecords{
        Core::Alloc::ScratchAllocator<AccessoryRecord>(scratchArena)
    };
    accessoryRecords.resize(static_cast<usize>(accessoryCount));
    for(__hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinaryV4& binaryRecord : accessoryRecords){
        if(!ReadPOD(binary, cursor, binaryRecord)){
            outState = DeformableSurfaceEditState{};
            return false;
        }
    }

    const usize stringTableOffset = cursor;
    for(usize i = 0u; i < outState.accessories.size(); ++i){
        if(!__hidden_deformable_surface_edit::BuildAccessoryRecordV4(
                accessoryRecords[i],
                binary,
                stringTableOffset,
                static_cast<usize>(stringTableByteCount),
                outState.accessories[i]
            )
        ){
            outState = DeformableSurfaceEditState{};
            return false;
        }
    }
    cursor += static_cast<usize>(stringTableByteCount);

    if(cursor != binary.size()){
        outState = DeformableSurfaceEditState{};
        return false;
    }
    if(!__hidden_deformable_surface_edit::ValidSurfaceEditState(outState)){
        outState = DeformableSurfaceEditState{};
        return false;
    }
    return true;
}

bool BuildSurfaceEditStateDebugDump(const DeformableSurfaceEditState& state, AString& outDump){
    outDump.clear();
    if(!__hidden_deformable_surface_edit::ValidSurfaceEditState(state))
        return false;

    outDump = StringFormat(
        "surface_edit_state version={} edits={} accessories={}\n",
        __hidden_deformable_surface_edit::s_SurfaceEditStateVersion,
        state.edits.size(),
        state.accessories.size()
    );
    for(usize i = 0u; i < state.edits.size(); ++i){
        const DeformableSurfaceEditRecord& record = state.edits[i];
        outDump += StringFormat(
            "edit[{}] id={} type=hole base_revision={} result_revision={} radius={} ellipse={} depth={} loop_cuts={} removed_triangles={} added_vertices={} added_triangles={}\n",
            i,
            record.editId,
            record.hole.baseEditRevision,
            record.result.editRevision,
            record.hole.radius,
            record.hole.ellipseRatio,
            record.hole.depth,
            record.hole.wallLoopCutCount,
            record.result.removedTriangleCount,
            record.result.addedVertexCount,
            record.result.addedTriangleCount
        );
    }
    for(usize i = 0u; i < state.accessories.size(); ++i){
        const DeformableAccessoryAttachmentRecord& accessory = state.accessories[i];
        const char* geometryPath = accessory.geometryVirtualPathText.empty()
            ? accessory.geometry.name().c_str()
            : accessory.geometryVirtualPathText.c_str()
        ;
        const char* materialPath = accessory.materialVirtualPathText.empty()
            ? accessory.material.name().c_str()
            : accessory.materialVirtualPathText.c_str()
        ;
        outDump += StringFormat(
            "accessory[{}] geometry={} material={} anchor_edit_id={} first_wall_vertex={} wall_vertex_count={} normal_offset={} uniform_scale={}\n",
            i,
            geometryPath,
            materialPath,
            accessory.anchorEditId,
            accessory.firstWallVertex,
            accessory.wallVertexCount,
            accessory.normalOffset,
            accessory.uniformScale
        );
    }
    return true;
}

namespace __hidden_deformable_surface_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool CommitDeformableRestSpaceHoleImpl(
    DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    const bool requireUploadedRuntimePayload,
    const u32 wallLoopCutCount,
    DeformableHoleEditResult* outResult)
{
    if(outResult)
        *outResult = DeformableHoleEditResult{};
    const bool validPayload = requireUploadedRuntimePayload
        ? ValidateUploadedRuntimePayload(instance)
        : ValidateRuntimePayload(instance)
    ;
    if(!validPayload
        || !ValidateParams(instance, params)
        || !ValidWallLoopCutCount(wallLoopCutCount)
        || (wallLoopCutCount != 0u && params.depth <= DeformableRuntime::s_Epsilon)
    )
        return false;

    const usize triangleCount = instance.indices.size() / 3u;
    u32 hitTriangleIndices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, params.posedHit.triangle, hitTriangleIndices))
        return false;

    f32 hitBary[3] = {};
    if(!DeformableValidation::NormalizeSourceBarycentric(params.posedHit.bary.values, hitBary))
        return false;

    HoleFrame frame;
    if(!BuildHoleFrame(instance, hitTriangleIndices, hitBary, frame))
        return false;

    SourceSample wallSourceSample{};
    if(!ResolveDeformableRestSurfaceSample(instance, params.posedHit.triangle, hitBary, wallSourceSample))
        return false;

    const f32 radiusX = params.radius;
    const f32 radiusY = params.radius * params.ellipseRatio;
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<u8> removeTriangle;
    removeTriangle.resize(triangleCount, 0u);
    DeformableEditMaskFlags removedEditMaskFlags = 0u;

    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        u32 indices[3] = {};
        if(!DeformableRuntime::ValidateTriangleIndex(instance, static_cast<u32>(triangle), indices))
            return false;

        const bool selectedTriangle = triangle == static_cast<usize>(params.posedHit.triangle);
        if(selectedTriangle
            || TriangleInsideFootprint(instance, frame, radiusX, radiusY, indices)
        ){
            const DeformableEditMaskFlags editMaskFlags =
                ResolveDeformableTriangleEditMask(instance, static_cast<u32>(triangle))
            ;
            if(!DeformableEditMaskAllowsCommit(editMaskFlags))
                return false;

            removedEditMaskFlags = static_cast<DeformableEditMaskFlags>(removedEditMaskFlags | editMaskFlags);
            removeTriangle[triangle] = 1u;
        }
    }

    if(removedEditMaskFlags == 0u)
        removedEditMaskFlags = s_DeformableEditMaskDefault;

    using EdgeRecordVector = Vector<EdgeRecord>;
    EdgeRecordVector boundaryEdges;
    u32 removedTriangleCount = 0u;
    if(!Core::Geometry::BuildBoundaryEdgesFromRemovedTriangles(
            instance.indices,
            removeTriangle,
            boundaryEdges,
            &removedTriangleCount
        )
    )
        return false;

    Vector<Float3U> restPositions;
    restPositions.reserve(instance.restVertices.size());
    for(const DeformableVertexRest& vertex : instance.restVertices)
        restPositions.push_back(vertex.position);

    Core::Geometry::MeshTopologyBoundaryLoopFrame topologyFrame;
    StoreFloat(frame.center, &topologyFrame.center);
    StoreFloat(frame.tangent, &topologyFrame.tangent);
    StoreFloat(frame.bitangent, &topologyFrame.bitangent);

    EdgeRecordVector orderedBoundaryEdges;
    if(!Core::Geometry::BuildOrderedBoundaryLoop(
            boundaryEdges,
            restPositions,
            topologyFrame,
            orderedBoundaryEdges
        )
    )
        return false;

    Vector<u32> newIndices;
    u32 newSourceTriangleCount = instance.sourceTriangleCount;
    if(instance.sourceSamples.empty() || instance.sourceSamples.size() != instance.restVertices.size() || newSourceTriangleCount == 0u)
        return false;

    const bool addWall = params.depth > DeformableRuntime::s_Epsilon;
    const usize wallBandCount = addWall ? static_cast<usize>(wallLoopCutCount) + 1u : 0u;
    usize wallVertexCount = 0u;
    usize totalWallVertexCount = 0u;
    if(addWall){
        if(wallBandCount == 0u
            || wallBandCount > Limit<usize>::s_Max / 6u
            || orderedBoundaryEdges.size() > Limit<usize>::s_Max / (6u * wallBandCount)
        )
            return false;

        wallVertexCount = orderedBoundaryEdges.size();
        if(wallVertexCount > Limit<usize>::s_Max / wallBandCount)
            return false;
        totalWallVertexCount = wallVertexCount * wallBandCount;
        if(totalWallVertexCount > Limit<usize>::s_Max - instance.restVertices.size())
            return false;
        if(instance.restVertices.size() + totalWallVertexCount > static_cast<usize>(Limit<u32>::s_Max))
            return false;
    }

    const usize removedIndexCount = static_cast<usize>(removedTriangleCount) * 3u;
    const usize wallIndexCount = addWall
        ? wallVertexCount * 6u * wallBandCount
        : 0u
    ;
    const usize keptIndexCount = instance.indices.size() - removedIndexCount;
    if(wallIndexCount > Limit<usize>::s_Max - keptIndexCount
        || keptIndexCount + wallIndexCount > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    usize reservedVertexCount = instance.restVertices.size();
    if(totalWallVertexCount > Limit<usize>::s_Max - reservedVertexCount)
        return false;
    reservedVertexCount += totalWallVertexCount;
    if(reservedVertexCount > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    Vector<DeformableVertexRest> newRestVertices = instance.restVertices;
    Vector<SkinInfluence4> newSkin;
    Vector<SourceSample> newSourceSamples;
    Vector<DeformableEditMaskFlags> newEditMaskPerTriangle;
    Vector<DeformableMorph> newMorphs;
    newRestVertices.reserve(reservedVertexCount);
    const bool hasEditMaskPerTriangle = !instance.editMaskPerTriangle.empty();
    if(hasEditMaskPerTriangle)
        newEditMaskPerTriangle.reserve((keptIndexCount + wallIndexCount) / 3u);
    if(addWall){
        newSkin = instance.skin;
        newSourceSamples = instance.sourceSamples;
        newMorphs = instance.morphs;
        if(!newSkin.empty())
            newSkin.reserve(reservedVertexCount);
        if(!newSourceSamples.empty())
            newSourceSamples.reserve(reservedVertexCount);
    }

    newIndices.reserve(keptIndexCount + wallIndexCount);
    u32 addedTriangleCount = 0;
    u32 addedVertexCount = 0;
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        if(removeTriangle[triangle] != 0u)
            continue;

        const usize indexBase = triangle * 3u;
        newIndices.push_back(instance.indices[indexBase + 0u]);
        newIndices.push_back(instance.indices[indexBase + 1u]);
        newIndices.push_back(instance.indices[indexBase + 2u]);
        if(hasEditMaskPerTriangle)
            newEditMaskPerTriangle.push_back(ResolveDeformableTriangleEditMask(instance, static_cast<u32>(triangle)));
    }

    u32 firstWallVertex = Limit<u32>::s_Max;
    u32 addedWallVertexCount = 0u;
    if(addWall){
        const usize boundaryVertexCount = orderedBoundaryEdges.size();
        firstWallVertex = static_cast<u32>(
            newRestVertices.size() + ((wallBandCount - 1u) * boundaryVertexCount)
        );
        addedWallVertexCount = static_cast<u32>(boundaryVertexCount);

        Vector<Core::Geometry::SurfacePatchWallVertex, Core::Alloc::ScratchAllocator<Core::Geometry::SurfacePatchWallVertex>> wallVertexPlan{
            Core::Alloc::ScratchAllocator<Core::Geometry::SurfacePatchWallVertex>(scratchArena)
        };
        wallVertexPlan.resize(totalWallVertexCount);
        Float3U frameNormal;
        StoreFloat(frame.normal, &frameNormal);
        if(!Core::Geometry::BuildSurfacePatchWallVertices(
                orderedBoundaryEdges,
                restPositions,
                topologyFrame,
                frameNormal,
                params.depth,
                wallBandCount,
                wallVertexPlan.data(),
                wallVertexPlan.size()
            )
        )
            return false;

        Vector<u32, Core::Alloc::ScratchAllocator<u32>> wallVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        wallVertices.resize(totalWallVertexCount, 0u);

        for(usize ringIndex = 0u; ringIndex < wallBandCount; ++ringIndex){
            const usize wallVertexBase = ringIndex * boundaryVertexCount;

            for(usize edgeIndex = 0; edgeIndex < boundaryVertexCount; ++edgeIndex){
                const Core::Geometry::SurfacePatchWallVertex& plannedVertex = wallVertexPlan[wallVertexBase + edgeIndex];
                if(!newSourceSamples.empty())
                    newSourceSamples[plannedVertex.sourceVertex] = wallSourceSample;

                Float4U innerColor;
                if(!BuildBlendedVertexColor(
                        newRestVertices,
                        plannedVertex.attributeVertices,
                        s_WallInnerInpaintWeights,
                        innerColor
                    )
                )
                    return false;

                SkinInfluence4 innerSkin;
                const SkinInfluence4* innerSkinPtr = nullptr;
                if(!newSkin.empty()){
                    if(!BuildBlendedSkinInfluence(
                            newSkin,
                            plannedVertex.attributeVertices,
                            s_WallInnerInpaintWeights,
                            innerSkin
                        )
                    )
                        return false;

                    innerSkinPtr = &innerSkin;
                }

                if(!AppendWallVertex(
                        newRestVertices,
                        newSkin,
                        newSourceSamples,
                        plannedVertex.sourceVertex,
                        innerSkinPtr,
                        wallSourceSample,
                        innerColor,
                        LoadFloat(plannedVertex.position),
                        LoadFloat(plannedVertex.normal),
                        LoadFloat(plannedVertex.tangent),
                        plannedVertex.uv0.x,
                        plannedVertex.uv0.y,
                        wallVertices[wallVertexBase + edgeIndex]
                    )
                )
                    return false;

                addedVertexCount += 1u;
            }
        }

        EdgeRecordVector bandOuterEdges = orderedBoundaryEdges;
        Vector<u32> ringVertices;
        ringVertices.reserve(boundaryVertexCount);
        for(usize ringIndex = 0u; ringIndex < wallBandCount; ++ringIndex){
            const usize wallVertexBase = ringIndex * boundaryVertexCount;
            ringVertices.clear();
            for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex)
                ringVertices.push_back(wallVertices[wallVertexBase + edgeIndex]);

            if(!TransferWallMorphDeltas(
                    newMorphs,
                    orderedBoundaryEdges,
                    ringVertices
                )
            )
                return false;

            u32 wallAddedTriangleCount = 0u;
            if(!Core::Geometry::AppendWallTrianglePairs(
                    bandOuterEdges,
                    ringVertices,
                    newIndices,
                    &wallAddedTriangleCount
                )
            )
                return false;
            if(hasEditMaskPerTriangle){
                for(u32 triangleOffset = 0u; triangleOffset < wallAddedTriangleCount; ++triangleOffset)
                    newEditMaskPerTriangle.push_back(removedEditMaskFlags);
            }
            addedTriangleCount += wallAddedTriangleCount;

            if(ringIndex + 1u < wallBandCount){
                if(!Core::Geometry::BuildSurfacePatchRingEdges(
                        wallVertices.data() + wallVertexBase,
                        boundaryVertexCount,
                        bandOuterEdges
                    )
                )
                    return false;
            }
        }
    }

    if(!RebuildRuntimeMeshTangentFrames(newRestVertices, newIndices))
        return false;

    if(!DeformableValidation::ValidRuntimePayloadArrays(
            newRestVertices,
            newIndices,
            newSourceTriangleCount,
            instance.skeletonJointCount,
            addWall ? newSkin : instance.skin,
            addWall ? newSourceSamples : instance.sourceSamples,
            hasEditMaskPerTriangle ? newEditMaskPerTriangle : instance.editMaskPerTriangle,
            addWall ? newMorphs : instance.morphs
        )
    )
        return false;

    instance.restVertices = Move(newRestVertices);
    if(addWall){
        instance.skin = Move(newSkin);
        instance.sourceSamples = Move(newSourceSamples);
        instance.morphs = Move(newMorphs);
    }
    if(hasEditMaskPerTriangle)
        instance.editMaskPerTriangle = Move(newEditMaskPerTriangle);
    instance.indices = Move(newIndices);
    instance.sourceTriangleCount = newSourceTriangleCount;
    ++instance.editRevision;
    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(instance.dirtyFlags | RuntimeMeshDirtyFlag::All);

    if(outResult){
        outResult->removedTriangleCount = removedTriangleCount;
        outResult->addedVertexCount = addedVertexCount;
        outResult->addedTriangleCount = addedTriangleCount;
        outResult->editRevision = instance.editRevision;
        outResult->firstWallVertex = firstWallVertex;
        outResult->wallVertexCount = addedWallVertexCount;
        outResult->wallLoopCutCount = wallLoopCutCount;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ReplayResultValidation{
enum Enum : u8{
    None,
    Shape,
    Exact
};
};

[[nodiscard]] bool ReplayResultMatchesValidation(
    const DeformableHoleEditResult& result,
    const DeformableSurfaceEditRecord& storedRecord,
    const ReplayResultValidation::Enum validation)
{
    switch(validation){
    case ReplayResultValidation::None:
        return true;
    case ReplayResultValidation::Shape:
        return ReplayResultShapeMatchesStoredRecord(result, storedRecord);
    case ReplayResultValidation::Exact:
        return ReplayResultMatchesStoredRecord(result, storedRecord);
    }
    return false;
}

[[nodiscard]] bool ReplaySurfaceEditRecord(
    DeformableRuntimeMeshInstance& replayInstance,
    const DeformableSurfaceEditRecord& storedRecord,
    DeformableSurfaceEditRecord& replayRecord,
    const ReplayResultValidation::Enum validation,
    DeformableHoleEditResult& outReplayResult)
{
    outReplayResult = DeformableHoleEditResult{};

    const usize triangleCount = replayInstance.indices.size() / 3u;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        DeformableHoleEditParams params;
        if(!BuildReplayHoleParams(replayInstance, replayRecord, triangle, params))
            continue;

        DeformableRuntimeMeshInstance candidateInstance = replayInstance;
        DeformableHoleEditResult candidateResult;
        if(!CommitDeformableRestSpaceHoleImpl(
                candidateInstance,
                params,
                false,
                replayRecord.hole.wallLoopCutCount,
                &candidateResult
            )
            || !RuntimeMeshHasWallTrianglePairs(candidateInstance, candidateResult)
            || !ReplayResultMatchesValidation(candidateResult, storedRecord, validation)
        )
            continue;

        replayInstance = Move(candidateInstance);
        outReplayResult = candidateResult;
        replayRecord.result = candidateResult;
        return true;
    }
    return false;
}

void AccumulateSurfaceEditReplayResult(
    DeformableSurfaceEditReplayResult& replay,
    const DeformableHoleEditResult& replayResult)
{
    ++replay.appliedEditCount;
    replay.topologyChanged = replay.topologyChanged || HoleResultChangesTopology(replayResult);
}

[[nodiscard]] bool RebuildReplayAccessories(
    const DeformableSurfaceEditState& sourceState,
    const DeformableSurfaceEditId removedAnchorEditId,
    DeformableSurfaceEditState& replayState,
    u32* outRemovedAccessoryCount)
{
    replayState.accessories.reserve(sourceState.accessories.size());
    for(const DeformableAccessoryAttachmentRecord& accessory : sourceState.accessories){
        if(outRemovedAccessoryCount && accessory.anchorEditId == removedAnchorEditId){
            ++(*outRemovedAccessoryCount);
            continue;
        }

        const DeformableSurfaceEditRecord* anchorEdit = FindEditRecordById(replayState, accessory.anchorEditId);
        if(!anchorEdit)
            return false;

        DeformableAccessoryAttachmentRecord replayAccessory = accessory;
        replayAccessory.firstWallVertex = anchorEdit->result.firstWallVertex;
        replayAccessory.wallVertexCount = anchorEdit->result.wallVertexCount;
        replayState.accessories.push_back(replayAccessory);
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ReplaySurfaceEditRecords(
    DeformableRuntimeMeshInstance& replayInstance,
    const DeformableSurfaceEditState& state,
    DeformableSurfaceEditReplayResult& result)
{
    result = DeformableSurfaceEditReplayResult{};
    for(const DeformableSurfaceEditRecord& record : state.edits){
        DeformableSurfaceEditRecord replayRecord = record;
        DeformableHoleEditResult replayResult;
        if(!ReplaySurfaceEditRecord(
            replayInstance,
            record,
            replayRecord,
            ReplayResultValidation::Exact,
            replayResult
        ))
            return false;

        AccumulateSurfaceEditReplayResult(result, replayResult);
    }
    result.finalEditRevision = replayInstance.editRevision;
    return true;
}

[[nodiscard]] bool BuildUndoSurfaceEditState(
    const DeformableSurfaceEditState& state,
    DeformableSurfaceEditState& outUndoState,
    DeformableSurfaceEditUndoResult& outResult,
    DeformableSurfaceEditRedoEntry* outRedoEntry)
{
    outUndoState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditUndoResult{};
    if(outRedoEntry)
        *outRedoEntry = DeformableSurfaceEditRedoEntry{};
    if(!ValidSurfaceEditState(state) || state.edits.empty())
        return false;

    const DeformableSurfaceEditId undoneEditId = state.edits.back().editId;
    if(outRedoEntry)
        outRedoEntry->edit = state.edits.back();

    outUndoState.edits.reserve(state.edits.size() - 1u);
    outUndoState.edits.insert(outUndoState.edits.end(), state.edits.begin(), state.edits.end() - 1u);

    outUndoState.accessories.reserve(state.accessories.size());
    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        if(accessory.anchorEditId == undoneEditId){
            ++outResult.removedAccessoryCount;
            if(outRedoEntry)
                outRedoEntry->accessories.push_back(accessory);
            continue;
        }
        outUndoState.accessories.push_back(accessory);
    }

    outResult.undoneEditId = undoneEditId;
    return ValidSurfaceEditState(outUndoState);
}

[[nodiscard]] u32 SurfaceEditStateFinalRevision(const DeformableSurfaceEditState& state){
    return state.edits.empty()
        ? 0u
        : state.edits.back().result.editRevision
    ;
}

[[nodiscard]] bool BuildRedoSurfaceEditState(
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditRedoEntry& redoEntry,
    DeformableSurfaceEditState& outRedoState,
    DeformableSurfaceEditRedoResult& outResult)
{
    outRedoState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditRedoResult{};
    if(!ValidSurfaceEditState(state)
        || !ValidEditRecord(redoEntry.edit)
        || redoEntry.edit.hole.baseEditRevision != SurfaceEditStateFinalRevision(state)
        || redoEntry.accessories.size() > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    for(const DeformableAccessoryAttachmentRecord& accessory : redoEntry.accessories){
        if(accessory.anchorEditId != redoEntry.edit.editId || !ValidAccessoryRecord(accessory))
            return false;
    }

    outRedoState.edits.reserve(state.edits.size() + 1u);
    outRedoState.edits.insert(outRedoState.edits.end(), state.edits.begin(), state.edits.end());
    outRedoState.edits.push_back(redoEntry.edit);

    outRedoState.accessories.reserve(state.accessories.size() + redoEntry.accessories.size());
    outRedoState.accessories.insert(outRedoState.accessories.end(), state.accessories.begin(), state.accessories.end());
    outRedoState.accessories.insert(outRedoState.accessories.end(), redoEntry.accessories.begin(), redoEntry.accessories.end());

    outResult.redoneEditId = redoEntry.edit.editId;
    outResult.restoredAccessoryCount = static_cast<u32>(redoEntry.accessories.size());
    return ValidSurfaceEditState(outRedoState);
}

[[nodiscard]] bool ReplaySurfaceEditRecordsWithoutEdit(
    DeformableRuntimeMeshInstance& replayInstance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    DeformableSurfaceEditState& outHealedState,
    DeformableSurfaceEditHealResult& outResult)
{
    outHealedState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditHealResult{};
    if(!ValidSurfaceEditState(state) || !ValidSurfaceEditId(editId))
        return false;

    const DeformableSurfaceEditRecord* healedEdit = FindEditRecordById(state, editId);
    if(!healedEdit)
        return false;

    outResult.healedEditId = editId;
    outResult.replay.topologyChanged = HoleResultChangesTopology(healedEdit->result);
    outHealedState.edits.reserve(state.edits.size() - 1u);
    for(const DeformableSurfaceEditRecord& record : state.edits){
        if(record.editId == editId)
            continue;
        if(replayInstance.editRevision == Limit<u32>::s_Max)
            return false;

        DeformableSurfaceEditRecord replayRecord = record;
        replayRecord.hole.baseEditRevision = replayInstance.editRevision;
        replayRecord.result.editRevision = replayInstance.editRevision + 1u;

        DeformableHoleEditResult replayResult;
        if(!ReplaySurfaceEditRecord(
            replayInstance,
            record,
            replayRecord,
            ReplayResultValidation::Shape,
            replayResult
        ))
            return false;

        outHealedState.edits.push_back(replayRecord);
        AccumulateSurfaceEditReplayResult(outResult.replay, replayResult);
    }

    if(!RebuildReplayAccessories(state, editId, outHealedState, &outResult.removedAccessoryCount))
        return false;

    outResult.replay.finalEditRevision = replayInstance.editRevision;
    return ValidSurfaceEditState(outHealedState);
}

[[nodiscard]] bool ReplaySurfaceEditRecordsWithResizedHole(
    DeformableRuntimeMeshInstance& replayInstance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    const f32 radius,
    const f32 ellipseRatio,
    const f32 depth,
    DeformableSurfaceEditState& outResizedState,
    DeformableSurfaceEditResizeResult& outResult)
{
    outResizedState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditResizeResult{};
    if(!ValidSurfaceEditState(state)
        || !ValidSurfaceEditId(editId)
        || !ValidateHoleShapeValues(radius, ellipseRatio, depth)
    )
        return false;

    const DeformableSurfaceEditRecord* resizedEdit = FindEditRecordById(state, editId);
    if(!resizedEdit || resizedEdit->type != DeformableSurfaceEditRecordType::Hole)
        return false;

    outResult.resizedEditId = editId;
    outResult.oldRadius = resizedEdit->hole.radius;
    outResult.oldEllipseRatio = resizedEdit->hole.ellipseRatio;
    outResult.oldDepth = resizedEdit->hole.depth;
    outResult.newRadius = radius;
    outResult.newEllipseRatio = ellipseRatio;
    outResult.newDepth = depth;

    bool resizedEditFound = false;
    bool replayDependsOnResizedEdit = false;
    outResizedState.edits.reserve(state.edits.size());
    for(const DeformableSurfaceEditRecord& record : state.edits){
        if(replayInstance.editRevision == Limit<u32>::s_Max)
            return false;

        DeformableSurfaceEditRecord replayRecord = record;
        replayRecord.hole.baseEditRevision = replayInstance.editRevision;
        replayRecord.result.editRevision = replayInstance.editRevision + 1u;

        const bool resizeThisRecord = record.editId == editId;
        if(resizeThisRecord){
            replayRecord.hole.radius = radius;
            replayRecord.hole.ellipseRatio = ellipseRatio;
            replayRecord.hole.depth = depth;
            resizedEditFound = true;
            replayDependsOnResizedEdit = true;
        }

        DeformableHoleEditResult replayResult;
        const ReplayResultValidation::Enum validation = resizeThisRecord
            ? ReplayResultValidation::None
            : replayDependsOnResizedEdit ? ReplayResultValidation::Shape : ReplayResultValidation::Exact
        ;
        if(!ReplaySurfaceEditRecord(replayInstance, record, replayRecord, validation, replayResult))
            return false;

        outResizedState.edits.push_back(replayRecord);
        AccumulateSurfaceEditReplayResult(outResult.replay, replayResult);
    }
    if(!resizedEditFound)
        return false;

    if(!RebuildReplayAccessories(state, 0u, outResizedState, nullptr))
        return false;

    outResult.replay.finalEditRevision = replayInstance.editRevision;
    return ValidSurfaceEditState(outResizedState);
}

[[nodiscard]] bool ReplaySurfaceEditRecordsWithMovedHole(
    DeformableRuntimeMeshInstance& replayInstance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    const DeformableSurfaceHoleEditRecord& moveTarget,
    DeformableSurfaceEditState& outMovedState,
    DeformableSurfaceEditMoveResult& outResult)
{
    outMovedState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditMoveResult{};
    if(!ValidSurfaceEditState(state)
        || !ValidSurfaceEditId(editId)
        || !ValidMoveTargetHoleRecord(moveTarget)
    )
        return false;

    const DeformableSurfaceEditRecord* movedEdit = FindEditRecordById(state, editId);
    if(!movedEdit || movedEdit->type != DeformableSurfaceEditRecordType::Hole)
        return false;

    outResult.movedEditId = editId;
    outResult.oldRestSample = movedEdit->hole.restSample;
    outResult.newRestSample = moveTarget.restSample;
    outResult.oldRestPosition = movedEdit->hole.restPosition;
    outResult.newRestPosition = moveTarget.restPosition;
    outResult.oldRestNormal = movedEdit->hole.restNormal;
    outResult.newRestNormal = moveTarget.restNormal;

    bool movedEditFound = false;
    bool replayDependsOnMovedEdit = false;
    outMovedState.edits.reserve(state.edits.size());
    for(const DeformableSurfaceEditRecord& record : state.edits){
        if(replayInstance.editRevision == Limit<u32>::s_Max)
            return false;

        DeformableSurfaceEditRecord replayRecord = record;
        replayRecord.hole.baseEditRevision = replayInstance.editRevision;
        replayRecord.result.editRevision = replayInstance.editRevision + 1u;

        const bool moveThisRecord = record.editId == editId;
        if(moveThisRecord){
            replayRecord.hole.restSample = moveTarget.restSample;
            replayRecord.hole.restPosition = moveTarget.restPosition;
            replayRecord.hole.restNormal = moveTarget.restNormal;
            movedEditFound = true;
            replayDependsOnMovedEdit = true;
        }

        DeformableHoleEditResult replayResult;
        const ReplayResultValidation::Enum validation = moveThisRecord
            ? ReplayResultValidation::None
            : replayDependsOnMovedEdit ? ReplayResultValidation::Shape : ReplayResultValidation::Exact
        ;
        if(!ReplaySurfaceEditRecord(replayInstance, record, replayRecord, validation, replayResult))
            return false;

        outMovedState.edits.push_back(replayRecord);
        AccumulateSurfaceEditReplayResult(outResult.replay, replayResult);
    }
    if(!movedEditFound)
        return false;

    if(!RebuildReplayAccessories(state, 0u, outMovedState, nullptr))
        return false;

    outResult.replay.finalEditRevision = replayInstance.editRevision;
    return ValidSurfaceEditState(outMovedState);
}

[[nodiscard]] bool ReplaySurfaceEditRecordsWithPatchedHole(
    DeformableRuntimeMeshInstance& replayInstance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    const DeformableSurfaceHoleEditRecord& patchTarget,
    const f32 radius,
    const f32 ellipseRatio,
    const f32 depth,
    DeformableSurfaceEditState& outPatchedState,
    DeformableSurfaceEditPatchResult& outResult)
{
    outPatchedState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditPatchResult{};
    if(!ValidSurfaceEditState(state)
        || !ValidSurfaceEditId(editId)
        || !ValidMoveTargetHoleRecord(patchTarget)
        || !ValidateHoleShapeValues(radius, ellipseRatio, depth)
    )
        return false;

    const DeformableSurfaceEditRecord* patchedEdit = FindEditRecordById(state, editId);
    if(!patchedEdit || patchedEdit->type != DeformableSurfaceEditRecordType::Hole)
        return false;

    outResult.patchedEditId = editId;
    outResult.oldRestSample = patchedEdit->hole.restSample;
    outResult.newRestSample = patchTarget.restSample;
    outResult.oldRestPosition = patchedEdit->hole.restPosition;
    outResult.newRestPosition = patchTarget.restPosition;
    outResult.oldRestNormal = patchedEdit->hole.restNormal;
    outResult.newRestNormal = patchTarget.restNormal;
    outResult.oldRadius = patchedEdit->hole.radius;
    outResult.oldEllipseRatio = patchedEdit->hole.ellipseRatio;
    outResult.oldDepth = patchedEdit->hole.depth;
    outResult.newRadius = radius;
    outResult.newEllipseRatio = ellipseRatio;
    outResult.newDepth = depth;

    bool patchedEditFound = false;
    bool replayDependsOnPatchedEdit = false;
    outPatchedState.edits.reserve(state.edits.size());
    for(const DeformableSurfaceEditRecord& record : state.edits){
        if(replayInstance.editRevision == Limit<u32>::s_Max)
            return false;

        DeformableSurfaceEditRecord replayRecord = record;
        replayRecord.hole.baseEditRevision = replayInstance.editRevision;
        replayRecord.result.editRevision = replayInstance.editRevision + 1u;

        const bool patchThisRecord = record.editId == editId;
        if(patchThisRecord){
            replayRecord.hole.restSample = patchTarget.restSample;
            replayRecord.hole.restPosition = patchTarget.restPosition;
            replayRecord.hole.restNormal = patchTarget.restNormal;
            replayRecord.hole.radius = radius;
            replayRecord.hole.ellipseRatio = ellipseRatio;
            replayRecord.hole.depth = depth;
            patchedEditFound = true;
            replayDependsOnPatchedEdit = true;
        }

        DeformableHoleEditResult replayResult;
        const ReplayResultValidation::Enum validation = patchThisRecord
            ? ReplayResultValidation::None
            : replayDependsOnPatchedEdit ? ReplayResultValidation::Shape : ReplayResultValidation::Exact
        ;
        if(!ReplaySurfaceEditRecord(replayInstance, record, replayRecord, validation, replayResult))
            return false;

        outPatchedState.edits.push_back(replayRecord);
        AccumulateSurfaceEditReplayResult(outResult.replay, replayResult);
    }
    if(!patchedEditFound)
        return false;

    if(!RebuildReplayAccessories(state, 0u, outPatchedState, nullptr))
        return false;

    outResult.replay.finalEditRevision = replayInstance.editRevision;
    return ValidSurfaceEditState(outPatchedState);
}

[[nodiscard]] bool ReplaySurfaceEditRecordsWithAddedLoopCut(
    DeformableRuntimeMeshInstance& replayInstance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    DeformableSurfaceEditState& outLoopCutState,
    DeformableSurfaceEditLoopCutResult& outResult)
{
    outLoopCutState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditLoopCutResult{};
    if(!ValidSurfaceEditState(state) || !ValidSurfaceEditId(editId))
        return false;

    const DeformableSurfaceEditRecord* loopCutEdit = FindEditRecordById(state, editId);
    if(!loopCutEdit
        || loopCutEdit->type != DeformableSurfaceEditRecordType::Hole
        || loopCutEdit->hole.depth <= DeformableRuntime::s_Epsilon
        || loopCutEdit->hole.wallLoopCutCount >= s_MaxWallLoopCutCount
    )
        return false;

    outResult.loopCutEditId = editId;
    outResult.oldLoopCutCount = loopCutEdit->hole.wallLoopCutCount;
    outResult.newLoopCutCount = loopCutEdit->hole.wallLoopCutCount + 1u;

    bool loopCutEditFound = false;
    bool replayDependsOnLoopCutEdit = false;
    outLoopCutState.edits.reserve(state.edits.size());
    for(const DeformableSurfaceEditRecord& record : state.edits){
        if(replayInstance.editRevision == Limit<u32>::s_Max)
            return false;

        DeformableSurfaceEditRecord replayRecord = record;
        replayRecord.hole.baseEditRevision = replayInstance.editRevision;
        replayRecord.result.editRevision = replayInstance.editRevision + 1u;

        const bool loopCutThisRecord = record.editId == editId;
        if(loopCutThisRecord){
            ++replayRecord.hole.wallLoopCutCount;
            loopCutEditFound = true;
            replayDependsOnLoopCutEdit = true;
        }

        DeformableHoleEditResult replayResult;
        const ReplayResultValidation::Enum validation = loopCutThisRecord
            ? ReplayResultValidation::None
            : replayDependsOnLoopCutEdit ? ReplayResultValidation::Shape : ReplayResultValidation::Exact
        ;
        if(!ReplaySurfaceEditRecord(replayInstance, record, replayRecord, validation, replayResult))
            return false;

        outLoopCutState.edits.push_back(replayRecord);
        AccumulateSurfaceEditReplayResult(outResult.replay, replayResult);
    }
    if(!loopCutEditFound)
        return false;

    if(!RebuildReplayAccessories(state, 0u, outLoopCutState, nullptr))
        return false;

    outResult.replay.finalEditRevision = replayInstance.editRevision;
    return ValidSurfaceEditState(outLoopCutState);
}

[[nodiscard]] bool PrepareCleanReplayInstance(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableRuntimeMeshInstance& cleanBaseInstance,
    DeformableRuntimeMeshInstance& outReplayInstance)
{
    outReplayInstance = DeformableRuntimeMeshInstance{};
    if(cleanBaseInstance.editRevision != 0u
        || cleanBaseInstance.source.name() != instance.source.name()
        || !ValidateRuntimePayload(cleanBaseInstance)
    )
        return false;

    outReplayInstance = cleanBaseInstance;
    outReplayInstance.entity = instance.entity;
    outReplayInstance.handle = instance.handle;
    outReplayInstance.restVertexBuffer = instance.restVertexBuffer;
    outReplayInstance.indexBuffer = instance.indexBuffer;
    outReplayInstance.deformedVertexBuffer = instance.deformedVertexBuffer;
    outReplayInstance.dirtyFlags = RuntimeMeshDirtyFlag::All;
    return ValidateRuntimePayload(outReplayInstance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CommitDeformableRestSpaceHole(
    DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    DeformableHoleEditResult* outResult)
{
    return __hidden_deformable_surface_edit::CommitDeformableRestSpaceHoleImpl(instance, params, true, 0u, outResult);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ApplySurfaceEditState(
    DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditReplayContext& context,
    DeformableSurfaceEditReplayResult* outResult)
{
    if(outResult)
        *outResult = DeformableSurfaceEditReplayResult{};

    if(!__hidden_deformable_surface_edit::ValidSurfaceEditState(state)
        || !__hidden_deformable_surface_edit::ValidateRuntimePayload(instance)
        || instance.editRevision != 0u
        || !__hidden_deformable_surface_edit::ReplayContextTargetsInstance(instance, context)
    )
        return false;

    DeformableRuntimeMeshInstance replayInstance = instance;
    DeformableSurfaceEditReplayResult result;
    if(!__hidden_deformable_surface_edit::ReplaySurfaceEditRecords(replayInstance, state, result))
        return false;

    if(!__hidden_deformable_surface_edit::ValidateReplayAccessories(replayInstance, state, context))
        return false;
    if(!__hidden_deformable_surface_edit::ValidateReplayAccessoryAssets(state, context))
        return false;

    instance = Move(replayInstance);
    __hidden_deformable_surface_edit::RestoreReplayAccessories(
        instance,
        state,
        context,
        result.restoredAccessoryCount
    );
    if(outResult)
        *outResult = result;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UndoLastSurfaceEdit(
    DeformableRuntimeMeshInstance& instance,
    const DeformableRuntimeMeshInstance& cleanBaseInstance,
    DeformableSurfaceEditState& state,
    DeformableSurfaceEditUndoResult* outResult,
    DeformableSurfaceEditHistory* history)
{
    if(outResult)
        *outResult = DeformableSurfaceEditUndoResult{};

    DeformableSurfaceEditState undoState;
    DeformableSurfaceEditUndoResult result;
    DeformableSurfaceEditRedoEntry redoEntry;
    if(!__hidden_deformable_surface_edit::BuildUndoSurfaceEditState(
            state,
            undoState,
            result,
            history ? &redoEntry : nullptr
        )
    )
        return false;

    DeformableRuntimeMeshInstance replayInstance;
    if(!__hidden_deformable_surface_edit::PrepareCleanReplayInstance(instance, cleanBaseInstance, replayInstance))
        return false;
    if(!__hidden_deformable_surface_edit::ReplaySurfaceEditRecords(replayInstance, undoState, result.replay))
        return false;
    if(!__hidden_deformable_surface_edit::ValidateReplayAccessoryAnchors(replayInstance, undoState))
        return false;

    state = Move(undoState);
    instance = Move(replayInstance);
    if(history)
        history->redoStack.push_back(Move(redoEntry));
    if(outResult)
        *outResult = result;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RedoLastSurfaceEdit(
    DeformableRuntimeMeshInstance& instance,
    const DeformableRuntimeMeshInstance& cleanBaseInstance,
    DeformableSurfaceEditState& state,
    DeformableSurfaceEditHistory& history,
    DeformableSurfaceEditRedoResult* outResult)
{
    if(outResult)
        *outResult = DeformableSurfaceEditRedoResult{};
    if(history.redoStack.empty())
        return false;

    DeformableSurfaceEditState redoState;
    DeformableSurfaceEditRedoResult result;
    if(!__hidden_deformable_surface_edit::BuildRedoSurfaceEditState(
            state,
            history.redoStack.back(),
            redoState,
            result
        )
    )
        return false;

    DeformableRuntimeMeshInstance replayInstance;
    if(!__hidden_deformable_surface_edit::PrepareCleanReplayInstance(instance, cleanBaseInstance, replayInstance))
        return false;
    if(!__hidden_deformable_surface_edit::ReplaySurfaceEditRecords(replayInstance, redoState, result.replay))
        return false;
    if(!__hidden_deformable_surface_edit::ValidateReplayAccessoryAnchors(replayInstance, redoState))
        return false;

    state = Move(redoState);
    instance = Move(replayInstance);
    history.redoStack.pop_back();
    if(outResult)
        *outResult = result;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool HealSurfaceEdit(
    DeformableRuntimeMeshInstance& instance,
    const DeformableRuntimeMeshInstance& cleanBaseInstance,
    DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    DeformableSurfaceEditHealResult* outResult)
{
    if(outResult)
        *outResult = DeformableSurfaceEditHealResult{};

    DeformableRuntimeMeshInstance replayInstance;
    if(!__hidden_deformable_surface_edit::PrepareCleanReplayInstance(instance, cleanBaseInstance, replayInstance))
        return false;

    DeformableSurfaceEditState healedState;
    DeformableSurfaceEditHealResult result;
    if(!__hidden_deformable_surface_edit::ReplaySurfaceEditRecordsWithoutEdit(
            replayInstance,
            state,
            editId,
            healedState,
            result
        )
    )
        return false;
    if(!__hidden_deformable_surface_edit::ValidateReplayAccessoryAnchors(replayInstance, healedState))
        return false;

    state = Move(healedState);
    instance = Move(replayInstance);
    if(outResult)
        *outResult = result;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ResizeSurfaceEdit(
    DeformableRuntimeMeshInstance& instance,
    const DeformableRuntimeMeshInstance& cleanBaseInstance,
    DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    const f32 radius,
    const f32 ellipseRatio,
    const f32 depth,
    DeformableSurfaceEditResizeResult* outResult)
{
    if(outResult)
        *outResult = DeformableSurfaceEditResizeResult{};

    DeformableRuntimeMeshInstance replayInstance;
    if(!__hidden_deformable_surface_edit::PrepareCleanReplayInstance(instance, cleanBaseInstance, replayInstance))
        return false;

    DeformableSurfaceEditState resizedState;
    DeformableSurfaceEditResizeResult result;
    if(!__hidden_deformable_surface_edit::ReplaySurfaceEditRecordsWithResizedHole(
            replayInstance,
            state,
            editId,
            radius,
            ellipseRatio,
            depth,
            resizedState,
            result
        )
    )
        return false;
    if(!__hidden_deformable_surface_edit::ValidateReplayAccessoryAnchors(replayInstance, resizedState))
        return false;

    state = Move(resizedState);
    instance = Move(replayInstance);
    if(outResult)
        *outResult = result;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MoveSurfaceEdit(
    DeformableRuntimeMeshInstance& instance,
    const DeformableRuntimeMeshInstance& cleanBaseInstance,
    DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    const DeformablePosedHit& targetHit,
    DeformableSurfaceEditMoveResult* outResult)
{
    if(outResult)
        *outResult = DeformableSurfaceEditMoveResult{};

    DeformableSurfaceHoleEditRecord moveTarget;
    if(!__hidden_deformable_surface_edit::BuildMoveTargetHoleRecord(instance, targetHit, moveTarget))
        return false;

    DeformableRuntimeMeshInstance replayInstance;
    if(!__hidden_deformable_surface_edit::PrepareCleanReplayInstance(instance, cleanBaseInstance, replayInstance))
        return false;

    DeformableSurfaceEditState movedState;
    DeformableSurfaceEditMoveResult result;
    if(!__hidden_deformable_surface_edit::ReplaySurfaceEditRecordsWithMovedHole(
            replayInstance,
            state,
            editId,
            moveTarget,
            movedState,
            result
        )
    )
        return false;
    if(!__hidden_deformable_surface_edit::ValidateReplayAccessoryAnchors(replayInstance, movedState))
        return false;

    state = Move(movedState);
    instance = Move(replayInstance);
    if(outResult)
        *outResult = result;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PatchSurfaceEdit(
    DeformableRuntimeMeshInstance& instance,
    const DeformableRuntimeMeshInstance& cleanBaseInstance,
    DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    const DeformablePosedHit& targetHit,
    const f32 radius,
    const f32 ellipseRatio,
    const f32 depth,
    DeformableSurfaceEditPatchResult* outResult)
{
    if(outResult)
        *outResult = DeformableSurfaceEditPatchResult{};

    DeformableSurfaceHoleEditRecord patchTarget;
    if(!__hidden_deformable_surface_edit::BuildMoveTargetHoleRecord(instance, targetHit, patchTarget))
        return false;

    DeformableRuntimeMeshInstance replayInstance;
    if(!__hidden_deformable_surface_edit::PrepareCleanReplayInstance(instance, cleanBaseInstance, replayInstance))
        return false;

    DeformableSurfaceEditState patchedState;
    DeformableSurfaceEditPatchResult result;
    if(!__hidden_deformable_surface_edit::ReplaySurfaceEditRecordsWithPatchedHole(
            replayInstance,
            state,
            editId,
            patchTarget,
            radius,
            ellipseRatio,
            depth,
            patchedState,
            result
        )
    )
        return false;
    if(!__hidden_deformable_surface_edit::ValidateReplayAccessoryAnchors(replayInstance, patchedState))
        return false;

    state = Move(patchedState);
    instance = Move(replayInstance);
    if(outResult)
        *outResult = result;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AddSurfaceEditLoopCut(
    DeformableRuntimeMeshInstance& instance,
    const DeformableRuntimeMeshInstance& cleanBaseInstance,
    DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    DeformableSurfaceEditLoopCutResult* outResult)
{
    if(outResult)
        *outResult = DeformableSurfaceEditLoopCutResult{};

    DeformableRuntimeMeshInstance replayInstance;
    if(!__hidden_deformable_surface_edit::PrepareCleanReplayInstance(instance, cleanBaseInstance, replayInstance))
        return false;

    DeformableSurfaceEditState loopCutState;
    DeformableSurfaceEditLoopCutResult result;
    if(!__hidden_deformable_surface_edit::ReplaySurfaceEditRecordsWithAddedLoopCut(
            replayInstance,
            state,
            editId,
            loopCutState,
            result
        )
    )
        return false;
    if(!__hidden_deformable_surface_edit::ValidateReplayAccessoryAnchors(replayInstance, loopCutState))
        return false;

    state = Move(loopCutState);
    instance = Move(replayInstance);
    if(outResult)
        *outResult = result;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

