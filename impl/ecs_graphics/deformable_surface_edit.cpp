// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_surface_edit.h"

#include "deformable_runtime_helpers.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_manager.h>
#include <core/ecs/entity.h>
#include <core/ecs/world.h>
#include <core/geometry/mesh_topology.h>
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

struct HoleFrame{
    SIMDVector center = VectorZero();
    SIMDVector normal = VectorZero();
    SIMDVector tangent = VectorZero();
    SIMDVector bitangent = VectorZero();
};

struct WallVertexFrame{
    SIMDVector normal = VectorZero();
    SIMDVector tangent = VectorZero();
};

struct SkinWeightSample{
    u16 joint = 0;
    f32 weight = 0.0f;
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
    outTangentVector = DeformableRuntime::ResolveFrameTangent(normalVector, tangentVector, fallbackTangent);
    outBitangentVector = DeformableRuntime::ResolveFrameBitangent(normalVector, outTangentVector, s_SIMDIdentityR1);
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

[[nodiscard]] u64 MakeEdgeKey(const u32 a, const u32 b){
    const u32 lo = a < b ? a : b;
    const u32 hi = a < b ? b : a;
    return (static_cast<u64>(lo) << 32u) | static_cast<u64>(hi);
}

template<typename EdgeMap>
void RegisterFullEdge(EdgeMap& edges, const u32 a, const u32 b){
    auto [it, inserted] = edges.emplace(MakeEdgeKey(a, b), EdgeRecord{});
    EdgeRecord& record = it.value();
    if(inserted){
        record.a = a;
        record.b = b;
    }
    ++record.fullCount;
}

template<typename EdgeMap>
[[nodiscard]] bool RegisterRemovedEdge(EdgeMap& edges, const u32 a, const u32 b){
    const auto found = edges.find(MakeEdgeKey(a, b));
    if(found == edges.end())
        return false;

    EdgeRecord& record = found.value();
    if(record.removedCount == 0u){
        record.a = a;
        record.b = b;
    }
    ++record.removedCount;
    return true;
}

template<typename VertexDegreeMap>
void IncrementVertexDegree(VertexDegreeMap& degrees, const u32 vertex){
    auto it = degrees.emplace(vertex, 0u).first;
    ++it.value();
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
    if(VectorGetX(Vector3LengthSq(rawNormal)) <= s_FrameEpsilon)
        return false;

    outFrame.center = BarycentricPoint(instance, triangleIndices, bary);
    outFrame.normal = DeformableRuntime::Normalize(rawNormal, VectorSet(0.0f, 0.0f, 1.0f, 0.0f));

    const DeformableVertexRest& vertex0 = instance.restVertices[triangleIndices[0]];
    const DeformableVertexRest& vertex1 = instance.restVertices[triangleIndices[1]];
    const DeformableVertexRest& vertex2 = instance.restVertices[triangleIndices[2]];
    SIMDVector tangentVector = VectorScale(VectorSet(vertex0.tangent.x, vertex0.tangent.y, vertex0.tangent.z, 0.0f), bary[0]);
    tangentVector = VectorMultiplyAdd(
        VectorSet(vertex1.tangent.x, vertex1.tangent.y, vertex1.tangent.z, 0.0f),
        VectorReplicate(bary[1]),
        tangentVector
    );
    tangentVector = VectorMultiplyAdd(
        VectorSet(vertex2.tangent.x, vertex2.tangent.y, vertex2.tangent.z, 0.0f),
        VectorReplicate(bary[2]),
        tangentVector
    );
    ResolveTangentBitangentVectors(outFrame.normal, tangentVector, edge0, outFrame.tangent, outFrame.bitangent);
    return VectorGetX(Vector3LengthSq(outFrame.normal)) > s_FrameEpsilon
        && VectorGetX(Vector3LengthSq(outFrame.tangent)) > s_FrameEpsilon
        && VectorGetX(Vector3LengthSq(outFrame.bitangent)) > s_FrameEpsilon
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

[[nodiscard]] bool ValidateHitRestSample(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePosedHit& hit)
{
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

[[nodiscard]] bool ValidateHitIdentity(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePosedHit& hit)
{
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

[[nodiscard]] bool ValidSurfaceEditId(const DeformableSurfaceEditId editId){
    return editId != 0u && editId != Limit<DeformableSurfaceEditId>::s_Max;
}

[[nodiscard]] bool ValidHoleEditResult(const DeformableHoleEditResult& result, const bool requireWall){
    if(result.editRevision == 0u || result.removedTriangleCount == 0u)
        return false;
    if(!ValidOptionalWallVertexSpan(result.firstWallVertex, result.wallVertexCount))
        return false;
    if(result.wallVertexCount == 0u)
        return !requireWall && result.addedTriangleCount == 0u;
    if(result.wallVertexCount > result.addedVertexCount)
        return false;
    if(result.wallVertexCount > Limit<u32>::s_Max / 2u)
        return false;
    if(result.addedTriangleCount != result.wallVertexCount * 2u)
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
    if(VectorGetX(Vector3LengthSq(rawNormal)) <= s_FrameEpsilon)
        return false;

    const SIMDVector triangleNormal = DeformableRuntime::Normalize(rawNormal, s_SIMDIdentityR2);
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

[[nodiscard]] bool AppendStringTablePath(
    Core::Assets::AssetBytes& stringTable,
    const CompactString& pathText,
    u32& outOffset)
{
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

    const SIMDVector normalVector = DeformableRuntime::Normalize(rawNormalVector, s_SIMDIdentityR2);
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
    if(!IsFinite(scale) || scale <= s_FrameEpsilon)
        return s_SIMDIdentityR3;

    SIMDVector rotationVector = VectorScale(axis, 1.0f / scale);
    return VectorSetW(rotationVector, scale * 0.5f);
}

[[nodiscard]] SIMDVector NormalizeRotationQuaternionVector(
    const SIMDVector rotationVector,
    const SIMDVector fallbackNormalVector)
{
    const f32 lengthSquared = VectorGetX(QuaternionLengthSq(rotationVector));
    if(!IsFinite(lengthSquared) || lengthSquared <= s_FrameEpsilon)
        return RotationFromPositiveZToNormalVector(fallbackNormalVector);

    return QuaternionNormalize(rotationVector);
}

[[nodiscard]] SIMDVector RotationFromFrame(const SIMDVector rawTangentVector, const SIMDVector rawNormalVector){
    if(!DeformableValidation::FiniteVector(rawNormalVector, 0x7u))
        return s_SIMDIdentityR3;

    const SIMDVector normalVector = DeformableRuntime::Normalize(rawNormalVector, s_SIMDIdentityR2);
    if(!DeformableValidation::FiniteVector(normalVector, 0x7u))
        return s_SIMDIdentityR3;

    const SIMDVector fallbackTangent = DeformableRuntime::FallbackTangent(normalVector);
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

[[nodiscard]] bool ActiveMorphDelta(const DeformableMorphDelta& delta){
    const SIMDVector epsilon = VectorReplicate(DeformableValidation::s_Epsilon);
    return !Vector3LessOrEqual(VectorAbs(LoadFloat(delta.deltaPosition)), epsilon)
        || !Vector3LessOrEqual(VectorAbs(LoadFloat(delta.deltaNormal)), epsilon)
        || !Vector4LessOrEqual(VectorAbs(LoadFloat(delta.deltaTangent)), epsilon)
    ;
}

void AccumulateMorphDelta(
    DeformableMorphDelta& target,
    const DeformableMorphDelta& source,
    const f32 weight)
{
    const SIMDVector weightVector = VectorReplicate(weight);
    StoreFloat(
        VectorMultiplyAdd(LoadFloat(source.deltaPosition), weightVector, LoadFloat(target.deltaPosition)),
        &target.deltaPosition
    );
    StoreFloat(
        VectorMultiplyAdd(LoadFloat(source.deltaNormal), weightVector, LoadFloat(target.deltaNormal)),
        &target.deltaNormal
    );
    StoreFloat(
        VectorMultiplyAdd(LoadFloat(source.deltaTangent), weightVector, LoadFloat(target.deltaTangent)),
        &target.deltaTangent
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
    DeformableMorphDelta blendedDelta{};
    blendedDelta.vertexId = outputVertex;
    bool hasDelta = false;
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

        AccumulateMorphDelta(blendedDelta, *sourceDelta, weight);
        hasDelta = true;
    }

    if(!hasDelta || !ActiveMorphDelta(blendedDelta))
        return true;

    outDeltas.push_back(blendedDelta);
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

[[nodiscard]] bool AccumulateSkinWeight(
    SkinWeightSample (&samples)[12],
    u32& sampleCount,
    const u16 joint,
    const f32 weight)
{
    if(!IsFinite(weight) || weight < 0.0f)
        return false;
    if(!DeformableValidation::ActiveWeight(weight))
        return true;

    for(u32 sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex){
        SkinWeightSample& sample = samples[sampleIndex];
        if(sample.joint != joint)
            continue;

        sample.weight += weight;
        return IsFinite(sample.weight);
    }

    if(sampleCount >= 12u)
        return false;

    samples[sampleCount].joint = joint;
    samples[sampleCount].weight = weight;
    ++sampleCount;
    return true;
}

[[nodiscard]] bool ExtractStrongestSkinWeight(
    SkinWeightSample (&samples)[12],
    const u32 sampleCount,
    SkinWeightSample& outSample)
{
    u32 bestIndex = Limit<u32>::s_Max;
    for(u32 sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex){
        const SkinWeightSample& sample = samples[sampleIndex];
        if(!DeformableValidation::ActiveWeight(sample.weight))
            continue;
        if(bestIndex == Limit<u32>::s_Max || sample.weight > samples[bestIndex].weight)
            bestIndex = sampleIndex;
    }
    if(bestIndex == Limit<u32>::s_Max)
        return false;

    outSample = samples[bestIndex];
    samples[bestIndex].weight = 0.0f;
    return true;
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

    SkinWeightSample samples[12] = {};
    u32 sampleCount = 0u;
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

        for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
            if(!AccumulateSkinWeight(
                    samples,
                    sampleCount,
                    sourceSkin.joint[influenceIndex],
                    sourceSkin.weight[influenceIndex] * sourceWeight
                )
            )
                return false;
        }
    }

    f32 selectedWeightSum = 0.0f;
    for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        SkinWeightSample selectedSample{};
        if(!ExtractStrongestSkinWeight(samples, sampleCount, selectedSample))
            break;

        outSkin.joint[influenceIndex] = selectedSample.joint;
        outSkin.weight[influenceIndex] = selectedSample.weight;
        selectedWeightSum += selectedSample.weight;
        if(!IsFinite(selectedWeightSum))
            return false;
    }

    if(!DeformableValidation::ActiveWeight(selectedWeightSum))
        return false;

    const f32 invSelectedWeightSum = 1.0f / selectedWeightSum;
    for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex)
        outSkin.weight[influenceIndex] *= invSelectedWeightSum;

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

    SIMDVector color = VectorZero();
    f32 weightSum = 0.0f;
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
        const SIMDVector sourceColorVector = LoadFloat(sourceColor);
        if(!DeformableValidation::FiniteVector(sourceColorVector, 0xFu))
            return false;

        color = VectorMultiplyAdd(sourceColorVector, VectorReplicate(sourceWeight), color);
        weightSum += sourceWeight;
        if(!DeformableValidation::FiniteVector(color, 0xFu) || !IsFinite(weightSum))
            return false;
    }

    if(!DeformableValidation::ActiveWeight(weightSum))
        return false;

    StoreFloat(VectorScale(color, 1.0f / weightSum), &outColor);
    return DeformableValidation::FiniteVector(LoadFloat(outColor), 0xFu);
}

[[nodiscard]] SIMDVector ProjectedEdgeDirection(
    const Vector<DeformableVertexRest>& vertices,
    const HoleFrame& frame,
    const EdgeRecord& edge)
{
    return DeformableRuntime::ResolveFrameTangent(
        frame.normal,
        VectorSubtract(LoadFloat(vertices[edge.b].position), LoadFloat(vertices[edge.a].position)),
        frame.tangent
    );
}

[[nodiscard]] bool BuildWallVertexFrame(
    const Vector<DeformableVertexRest>& vertices,
    const HoleFrame& frame,
    const EdgeRecord& previousEdge,
    const EdgeRecord& currentEdge,
    WallVertexFrame& outFrame)
{
    const SIMDVector frameNormal = frame.normal;
    const SIMDVector previousDirection = ProjectedEdgeDirection(vertices, frame, previousEdge);
    const SIMDVector currentDirection = ProjectedEdgeDirection(vertices, frame, currentEdge);
    const SIMDVector previousInwardVector = DeformableRuntime::Normalize(
        Vector3Cross(frameNormal, previousDirection),
        frame.bitangent
    );
    const SIMDVector currentInwardVector = DeformableRuntime::Normalize(
        Vector3Cross(frameNormal, currentDirection),
        previousInwardVector
    );
    SIMDVector normalVector = DeformableRuntime::Normalize(
        VectorAdd(previousInwardVector, currentInwardVector),
        currentInwardVector
    );

    const SIMDVector centerOffset = VectorSubtract(frame.center, LoadFloat(vertices[currentEdge.a].position));
    if(VectorGetX(Vector3Dot(normalVector, centerOffset)) < 0.0f)
        normalVector = VectorScale(normalVector, -1.0f);

    const SIMDVector tangentVector = DeformableRuntime::ResolveFrameTangent(
        normalVector,
        Vector3Cross(frameNormal, normalVector),
        currentDirection
    );

    outFrame.normal = normalVector;
    outFrame.tangent = tangentVector;
    return VectorGetX(Vector3LengthSq(outFrame.normal)) > s_FrameEpsilon
        && VectorGetX(Vector3LengthSq(outFrame.tangent)) > s_FrameEpsilon
        && Abs(VectorGetX(Vector3Dot(outFrame.normal, outFrame.tangent))) <= 0.001f
    ;
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
    StoreFloat(tangent, &wallVertex.tangent);
    wallVertex.tangent.w = DeformableRuntime::TangentHandedness(wallVertex.tangent.w, 1.0f);
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

[[nodiscard]] bool RebuildRuntimeMeshTangentFrames(
    Vector<DeformableVertexRest>& vertices,
    const Vector<u32>& indices)
{
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

    StoreFloat(frame.center, &outPreview.center);
    StoreFloat(frame.normal, &outPreview.normal);
    StoreFloat(frame.tangent, &outPreview.tangent);
    StoreFloat(frame.bitangent, &outPreview.bitangent);
    outPreview.center.w = 1.0f;
    outPreview.normal.w = 0.0f;
    outPreview.tangent.w = 0.0f;
    outPreview.bitangent.w = 0.0f;
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

    SIMDVector rimCenter = VectorZero();
    SIMDVector innerCenter = VectorZero();
    SIMDVector firstRimPosition = VectorZero();
    for(usize pairIndex = 0u; pairIndex < wallVertexCount; ++pairIndex){
        const usize pairIndexBase = wallIndexBase + (pairIndex * 6u);
        const usize rimVertexIndex = static_cast<usize>(instance.indices[pairIndexBase + 0u]);
        const usize innerVertexIndex = firstWallVertex + pairIndex;
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
    SIMDVector normal = DeformableRuntime::Normalize(
        VectorSubtract(rimCenter, innerCenter),
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
    );
    if(!__hidden_deformable_surface_edit::FiniteVec3(normal)
        || VectorGetX(Vector3LengthSq(normal)) <= DeformableRuntime::s_FrameEpsilon
    )
        return false;

    const SIMDVector accessoryPosition = VectorMultiplyAdd(
        normal,
        VectorReplicate(attachment.normalOffset()),
        rimCenter
    );
    if(!__hidden_deformable_surface_edit::FiniteVec3(accessoryPosition))
        return false;

    const SIMDVector tangent = DeformableRuntime::ResolveFrameTangent(
        normal,
        VectorSubtract(firstRimPosition, rimCenter),
        DeformableRuntime::FallbackTangent(normal)
    );

    StoreFloat(accessoryPosition, &outTransform.position);
    outTransform.position.w = 0.0f;
    StoreFloat(
        __hidden_deformable_surface_edit::RotationFromFrame(tangent, normal),
        &outTransform.rotation
    );
    const f32 uniformScale = attachment.uniformScale();
    outTransform.scale = Float4(uniformScale, uniformScale, uniformScale);
    return true;
}

bool SerializeSurfaceEditState(
    const DeformableSurfaceEditState& state,
    Core::Assets::AssetBytes& outBinary)
{
    outBinary.clear();
    if(!__hidden_deformable_surface_edit::ValidSurfaceEditState(state))
        return false;

    Vector<__hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinaryV4> accessoryRecords;
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

bool DeserializeSurfaceEditState(
    const Core::Assets::AssetBytes& binary,
    DeformableSurfaceEditState& outState)
{
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
    Vector<__hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinaryV4> accessoryRecords;
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

bool BuildSurfaceEditStateDebugDump(
    const DeformableSurfaceEditState& state,
    AString& outDump)
{
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
            "edit[{}] id={} type=hole base_revision={} result_revision={} radius={} ellipse={} depth={} removed_triangles={} added_vertices={} added_triangles={}\n",
            i,
            record.editId,
            record.hole.baseEditRevision,
            record.result.editRevision,
            record.hole.radius,
            record.hole.ellipseRatio,
            record.hole.depth,
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
    DeformableHoleEditResult* outResult)
{
    if(outResult)
        *outResult = DeformableHoleEditResult{};
    const bool validPayload = requireUploadedRuntimePayload
        ? ValidateUploadedRuntimePayload(instance)
        : ValidateRuntimePayload(instance)
    ;
    if(!validPayload || !ValidateParams(instance, params))
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
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> removeTriangle{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    removeTriangle.resize(triangleCount, 0u);
    DeformableEditMaskFlags removedEditMaskFlags = 0u;

    using EdgeRecordVector = Vector<EdgeRecord>;
    using EdgeRecordMap = HashMap<
        u64,
        EdgeRecord,
        Hasher<u64>,
        EqualTo<u64>,
        Core::Alloc::ScratchAllocator<Pair<const u64, EdgeRecord>>
    >;
    using VertexDegreeMap = HashMap<
        u32,
        u32,
        Hasher<u32>,
        EqualTo<u32>,
        Core::Alloc::ScratchAllocator<Pair<const u32, u32>>
    >;

    EdgeRecordMap edges(
        0,
        Hasher<u64>(),
        EqualTo<u64>(),
        Core::Alloc::ScratchAllocator<Pair<const u64, EdgeRecord>>(scratchArena)
    );
    edges.reserve(instance.indices.size());

    u32 removedTriangleCount = 0;
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        u32 indices[3] = {};
        if(!DeformableRuntime::ValidateTriangleIndex(instance, static_cast<u32>(triangle), indices))
            return false;

        RegisterFullEdge(edges, indices[0], indices[1]);
        RegisterFullEdge(edges, indices[1], indices[2]);
        RegisterFullEdge(edges, indices[2], indices[0]);

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
            ++removedTriangleCount;

            if(!RegisterRemovedEdge(edges, indices[0], indices[1])
                || !RegisterRemovedEdge(edges, indices[1], indices[2])
                || !RegisterRemovedEdge(edges, indices[2], indices[0])
            )
                return false;
        }
    }

    if(removedTriangleCount == 0u || removedTriangleCount >= triangleCount)
        return false;
    if(removedEditMaskFlags == 0u)
        removedEditMaskFlags = s_DeformableEditMaskDefault;

    EdgeRecordVector boundaryEdges;
    boundaryEdges.reserve(removedTriangleCount * 3u);
    VertexDegreeMap boundaryDegrees(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        Core::Alloc::ScratchAllocator<Pair<const u32, u32>>(scratchArena)
    );
    boundaryDegrees.reserve(removedTriangleCount * 3u);
    for(const auto& [edgeKey, edge] : edges){
        static_cast<void>(edgeKey);
        if(edge.removedCount == 0u)
            continue;
        if(edge.removedCount > edge.fullCount || edge.fullCount > 2u)
            return false;
        if(edge.removedCount == 1u){
            if(edge.fullCount != 2u)
                return false;
            boundaryEdges.push_back(edge);
            IncrementVertexDegree(boundaryDegrees, edge.a);
            IncrementVertexDegree(boundaryDegrees, edge.b);
        }
    }
    if(boundaryEdges.empty())
        return false;
    for(const auto& [vertex, degree] : boundaryDegrees){
        static_cast<void>(vertex);
        if(degree != 2u)
            return false;
    }
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
    usize wallVertexCount = 0u;
    if(addWall){
        if(orderedBoundaryEdges.size() > Limit<usize>::s_Max / 6u)
            return false;

        wallVertexCount = orderedBoundaryEdges.size();
        if(wallVertexCount > Limit<usize>::s_Max - instance.restVertices.size())
            return false;
        if(instance.restVertices.size() + wallVertexCount > static_cast<usize>(Limit<u32>::s_Max))
            return false;
    }

    const usize removedIndexCount = static_cast<usize>(removedTriangleCount) * 3u;
    const usize wallIndexCount = addWall
        ? orderedBoundaryEdges.size() * 6u
        : 0u
    ;
    const usize keptIndexCount = instance.indices.size() - removedIndexCount;
    if(wallIndexCount > Limit<usize>::s_Max - keptIndexCount
        || keptIndexCount + wallIndexCount > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    usize reservedVertexCount = instance.restVertices.size();
    if(wallVertexCount > Limit<usize>::s_Max - reservedVertexCount)
        return false;
    reservedVertexCount += wallVertexCount;
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
        firstWallVertex = static_cast<u32>(newRestVertices.size());
        addedWallVertexCount = static_cast<u32>(boundaryVertexCount);
        const SIMDVector frameNormal = frame.normal;
        Vector<f32, Core::Alloc::ScratchAllocator<f32>> boundaryU{
            Core::Alloc::ScratchAllocator<f32>(scratchArena)
        };
        boundaryU.resize(boundaryVertexCount, 0.0f);

        f32 boundaryLength = 0.0f;
        for(usize edgeIndex = 0; edgeIndex < boundaryVertexCount; ++edgeIndex){
            boundaryU[edgeIndex] = boundaryLength;

            const EdgeRecord& edge = orderedBoundaryEdges[edgeIndex];
            const SIMDVector edgeDelta = DeformableRuntime::ProjectOntoFramePlane(
                VectorSubtract(LoadFloat(newRestVertices[edge.b].position), LoadFloat(newRestVertices[edge.a].position)),
                frameNormal
            );
            const f32 edgeLength = VectorGetX(Vector3Length(edgeDelta));
            if(!IsFinite(edgeLength) || edgeLength <= DeformableRuntime::s_Epsilon)
                return false;
            boundaryLength += edgeLength;
            if(!IsFinite(boundaryLength))
                return false;
        }

        if(boundaryLength <= DeformableRuntime::s_Epsilon)
            return false;

        Vector<u32> innerVertices;
        innerVertices.resize(boundaryVertexCount, 0u);

        for(usize edgeIndex = 0; edgeIndex < boundaryVertexCount; ++edgeIndex){
            const usize previousEdgeIndex = edgeIndex == 0u ? boundaryVertexCount - 1u : edgeIndex - 1u;
            const EdgeRecord& edge = orderedBoundaryEdges[edgeIndex];

            WallVertexFrame vertexFrame;
            if(!BuildWallVertexFrame(
                    newRestVertices,
                    frame,
                    orderedBoundaryEdges[previousEdgeIndex],
                    edge,
                    vertexFrame
                )
            )
                return false;

            const SIMDVector rimPosition = LoadFloat(newRestVertices[edge.a].position);
            const SIMDVector innerPosition = VectorMultiplyAdd(
                frame.normal,
                VectorReplicate(-params.depth),
                rimPosition
            );
            const f32 uvU = boundaryU[edgeIndex] / boundaryLength;
            if(!newSourceSamples.empty())
                newSourceSamples[edge.a] = wallSourceSample;

            const u32 innerAttributeVertices[3] = {
                orderedBoundaryEdges[previousEdgeIndex].a,
                edge.a,
                edge.b,
            };
            Float4U innerColor;
            if(!BuildBlendedVertexColor(
                    newRestVertices,
                    innerAttributeVertices,
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
                        innerAttributeVertices,
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
                    edge.a,
                    innerSkinPtr,
                    wallSourceSample,
                    innerColor,
                    innerPosition,
                    vertexFrame.normal,
                    vertexFrame.tangent,
                    uvU,
                    1.0f,
                    innerVertices[edgeIndex]
                )
            )
                return false;

            addedVertexCount += 1u;
        }

        if(!TransferWallMorphDeltas(
                newMorphs,
                orderedBoundaryEdges,
                innerVertices
            )
        )
            return false;

        u32 wallAddedTriangleCount = 0u;
        if(!Core::Geometry::AppendWallTrianglePairs(
                orderedBoundaryEdges,
                innerVertices,
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
    }

    if(!RebuildRuntimeMeshTangentFrames(newRestVertices, newIndices))
        return false;

    if(!DeformableValidation::ValidRuntimePayloadArrays(
            newRestVertices,
            newIndices,
            newSourceTriangleCount,
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
        bool replayedRecord = false;
        DeformableHoleEditResult replayResult;
        const usize triangleCount = replayInstance.indices.size() / 3u;
        for(usize triangle = 0u; triangle < triangleCount; ++triangle){
            DeformableHoleEditParams params;
            if(!BuildReplayHoleParams(replayInstance, record, triangle, params))
                continue;

            DeformableRuntimeMeshInstance candidateInstance = replayInstance;
            DeformableHoleEditResult candidateResult;
            if(!CommitDeformableRestSpaceHoleImpl(
                    candidateInstance,
                    params,
                    false,
                    &candidateResult
                )
                || !ReplayResultMatchesStoredRecord(candidateResult, record)
                || !RuntimeMeshHasWallTrianglePairs(candidateInstance, candidateResult)
            )
                continue;

            replayInstance = Move(candidateInstance);
            replayResult = candidateResult;
            replayedRecord = true;
            break;
        }
        if(!replayedRecord)
            return false;

        ++result.appliedEditCount;
        result.topologyChanged = result.topologyChanged || HoleResultChangesTopology(replayResult);
    }
    result.finalEditRevision = replayInstance.editRevision;
    return true;
}

[[nodiscard]] bool BuildUndoSurfaceEditState(
    const DeformableSurfaceEditState& state,
    DeformableSurfaceEditState& outUndoState,
    DeformableSurfaceEditUndoResult& outResult)
{
    outUndoState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditUndoResult{};
    if(!ValidSurfaceEditState(state) || state.edits.empty())
        return false;

    const DeformableSurfaceEditId undoneEditId = state.edits.back().editId;
    outUndoState.edits.reserve(state.edits.size() - 1u);
    for(usize editIndex = 0u; editIndex + 1u < state.edits.size(); ++editIndex)
        outUndoState.edits.push_back(state.edits[editIndex]);

    outUndoState.accessories.reserve(state.accessories.size());
    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        if(accessory.anchorEditId == undoneEditId){
            ++outResult.removedAccessoryCount;
            continue;
        }
        outUndoState.accessories.push_back(accessory);
    }

    outResult.undoneEditId = undoneEditId;
    return ValidSurfaceEditState(outUndoState);
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

        bool replayedRecord = false;
        DeformableHoleEditResult replayResult;
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
                    &candidateResult
                )
                || !ReplayResultShapeMatchesStoredRecord(candidateResult, record)
                || !RuntimeMeshHasWallTrianglePairs(candidateInstance, candidateResult)
            )
                continue;

            replayInstance = Move(candidateInstance);
            replayResult = candidateResult;
            replayRecord.result = candidateResult;
            replayedRecord = true;
            break;
        }
        if(!replayedRecord)
            return false;

        outHealedState.edits.push_back(replayRecord);
        ++outResult.replay.appliedEditCount;
        outResult.replay.topologyChanged = outResult.replay.topologyChanged
            || HoleResultChangesTopology(replayResult)
        ;
    }

    outHealedState.accessories.reserve(state.accessories.size());
    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        if(accessory.anchorEditId == editId){
            ++outResult.removedAccessoryCount;
            continue;
        }

        const DeformableSurfaceEditRecord* anchorEdit =
            FindEditRecordById(outHealedState, accessory.anchorEditId)
        ;
        if(!anchorEdit)
            return false;

        DeformableAccessoryAttachmentRecord replayAccessory = accessory;
        replayAccessory.firstWallVertex = anchorEdit->result.firstWallVertex;
        replayAccessory.wallVertexCount = anchorEdit->result.wallVertexCount;
        outHealedState.accessories.push_back(replayAccessory);
    }

    outResult.replay.finalEditRevision = replayInstance.editRevision;
    return ValidSurfaceEditState(outHealedState);
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
    return __hidden_deformable_surface_edit::CommitDeformableRestSpaceHoleImpl(instance, params, true, outResult);
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
    DeformableSurfaceEditUndoResult* outResult)
{
    if(outResult)
        *outResult = DeformableSurfaceEditUndoResult{};

    DeformableSurfaceEditState undoState;
    DeformableSurfaceEditUndoResult result;
    if(!__hidden_deformable_surface_edit::BuildUndoSurfaceEditState(state, undoState, result))
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

