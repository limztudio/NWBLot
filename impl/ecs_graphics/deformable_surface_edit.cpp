// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_surface_edit.h"

#include "deformable_runtime_helpers.h"

#include <core/alloc/scratch.h>
#include <impl/assets_graphics/deformable_geometry_validation.h>
#include <global/algorithm.h>
#include <global/binary.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_surface_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace DeformableRuntime;

static constexpr f32 s_WallInnerInpaintWeights[3] = { 0.25f, 0.5f, 0.25f };
static constexpr u32 s_SurfaceEditStateMagic = 0x53454631u; // SEF1
static constexpr u32 s_SurfaceEditStateVersion = 3u;
static constexpr u32 s_MinWallLoopVertexCount = 3u;

struct HoleFrame{
    SIMDVector center = VectorZero();
    SIMDVector normal = VectorZero();
    SIMDVector tangent = VectorZero();
    SIMDVector bitangent = VectorZero();
};

struct EdgeRecord{
    u32 a = 0;
    u32 b = 0;
    u32 fullCount = 0;
    u32 removedCount = 0;
};

struct BoundaryVertexEdges{
    usize edgeIndices[2] = { Limit<usize>::s_Max, Limit<usize>::s_Max };
    u32 count = 0;
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

struct SurfaceEditStateHeader{
    u32 magic = s_SurfaceEditStateMagic;
    u32 version = s_SurfaceEditStateVersion;
    u64 editCount = 0;
    u64 accessoryCount = 0;
};
static_assert(IsStandardLayout_V<SurfaceEditStateHeader>, "SurfaceEditStateHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SurfaceEditStateHeader>, "SurfaceEditStateHeader must stay binary-serializable");

struct SurfaceEditAccessoryRecordBinary{
    u32 editRevision = 0;
    u32 firstWallVertex = Limit<u32>::s_Max;
    u32 wallVertexCount = 0;
    f32 normalOffset = 0.0f;
    f32 uniformScale = 1.0f;
    NameHash geometryNameHash = {};
    NameHash materialNameHash = {};
};
static_assert(
    IsStandardLayout_V<SurfaceEditAccessoryRecordBinary>,
    "SurfaceEditAccessoryRecordBinary must stay binary-serializable"
);
static_assert(
    IsTriviallyCopyable_V<SurfaceEditAccessoryRecordBinary>,
    "SurfaceEditAccessoryRecordBinary must stay binary-serializable"
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

template<typename VertexEdgeMap>
[[nodiscard]] bool RegisterBoundaryVertexEdge(VertexEdgeMap& vertexEdges, const u32 vertex, const usize edgeIndex){
    auto it = vertexEdges.emplace(vertex, BoundaryVertexEdges{}).first;
    BoundaryVertexEdges& adjacency = it.value();
    if(adjacency.count >= LengthOf(adjacency.edgeIndices))
        return false;

    adjacency.edgeIndices[adjacency.count] = edgeIndex;
    ++adjacency.count;
    return true;
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
        instance.morphs
    );
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
    const u32 wallVertexCountValue)
{
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

    const usize indexBase = instance.indices.size() - wallIndexCount;
    return RuntimeMeshWallTrianglePairsMatchAt(instance, indexBase, firstWallVertex, wallVertexCount);
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
    const u32 editRevision,
    const u32 firstWallVertex,
    const u32 wallVertexCount,
    const f32 normalOffset,
    const f32 uniformScale)
{
    return editRevision != 0u
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
            attachment.editRevision,
            attachment.firstWallVertex,
            attachment.wallVertexCount,
            attachment.normalOffset(),
            attachment.uniformScale()
        )
    ;
}

[[nodiscard]] bool ValidAccessoryRecord(const DeformableAccessoryAttachmentRecord& record){
    return ValidAccessoryAttachmentValues(
            record.editRevision,
            record.firstWallVertex,
            record.wallVertexCount,
            record.normalOffset,
            record.uniformScale
        )
        && record.geometry.valid()
        && record.material.valid()
    ;
}

[[nodiscard]] bool EditRecordMatchesAccessory(
    const DeformableSurfaceEditRecord& record,
    const DeformableAccessoryAttachmentRecord& accessory)
{
    return record.result.editRevision == accessory.editRevision
        && record.result.firstWallVertex == accessory.firstWallVertex
        && record.result.wallVertexCount == accessory.wallVertexCount
    ;
}

[[nodiscard]] bool ValidSurfaceEditState(const DeformableSurfaceEditState& state){
    u32 expectedBaseEditRevision = 0u;
    for(const DeformableSurfaceEditRecord& record : state.edits){
        if(!ValidEditRecord(record))
            return false;
        if(record.hole.baseEditRevision != expectedBaseEditRevision)
            return false;
        expectedBaseEditRevision = record.result.editRevision;
    }
    const DeformableSurfaceEditRecord* latestCommittedEdit = state.edits.empty()
        ? nullptr
        : &state.edits.back()
    ;
    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        if(!ValidAccessoryRecord(accessory)
            || accessory.editRevision != expectedBaseEditRevision
            || !latestCommittedEdit
            || !EditRecordMatchesAccessory(*latestCommittedEdit, accessory)
        )
            return false;
    }
    return true;
}

[[nodiscard]] bool BuildAccessoryBinaryRecord(
    const DeformableAccessoryAttachmentRecord& record,
    SurfaceEditAccessoryRecordBinary& outRecord)
{
    outRecord = SurfaceEditAccessoryRecordBinary{};
    if(!ValidAccessoryRecord(record))
        return false;

    outRecord.editRevision = record.editRevision;
    outRecord.firstWallVertex = record.firstWallVertex;
    outRecord.wallVertexCount = record.wallVertexCount;
    outRecord.normalOffset = record.normalOffset;
    outRecord.uniformScale = record.uniformScale;
    outRecord.geometryNameHash = record.geometry.name().hash();
    outRecord.materialNameHash = record.material.name().hash();
    return true;
}

[[nodiscard]] bool BuildAccessoryRecord(
    const SurfaceEditAccessoryRecordBinary& binary,
    DeformableAccessoryAttachmentRecord& outRecord)
{
    outRecord = DeformableAccessoryAttachmentRecord{};
    outRecord.editRevision = binary.editRevision;
    outRecord.firstWallVertex = binary.firstWallVertex;
    outRecord.wallVertexCount = binary.wallVertexCount;
    outRecord.normalOffset = binary.normalOffset;
    outRecord.uniformScale = binary.uniformScale;
    outRecord.geometry.virtualPath = Name(binary.geometryNameHash);
    outRecord.material.virtualPath = Name(binary.materialNameHash);
    return ValidAccessoryRecord(outRecord);
}

[[nodiscard]] bool ComputeSurfaceEditStateBinarySize(
    const u64 editCount,
    const u64 accessoryCount,
    usize& outSize)
{
    outSize = sizeof(SurfaceEditStateHeader);
    if(editCount > static_cast<u64>(Limit<usize>::s_Max / sizeof(DeformableSurfaceEditRecord)))
        return false;
    if(accessoryCount > static_cast<u64>(Limit<usize>::s_Max / sizeof(SurfaceEditAccessoryRecordBinary)))
        return false;

    const usize editBytes = static_cast<usize>(editCount) * sizeof(DeformableSurfaceEditRecord);
    const usize accessoryBytes = static_cast<usize>(accessoryCount) * sizeof(SurfaceEditAccessoryRecordBinary);
    if(editBytes > Limit<usize>::s_Max - outSize)
        return false;
    outSize += editBytes;
    if(accessoryBytes > Limit<usize>::s_Max - outSize)
        return false;
    outSize += accessoryBytes;
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

template<typename EdgeVector>
[[nodiscard]] f32 ProjectedSignedLoopArea(
    const DeformableRuntimeMeshInstance& instance,
    const HoleFrame& frame,
    const EdgeVector& orderedEdges)
{
    f32 signedArea = 0.0f;
    for(const EdgeRecord& edge : orderedEdges){
        const SIMDVector aOffset = VectorSubtract(LoadFloat(instance.restVertices[edge.a].position), frame.center);
        const SIMDVector bOffset = VectorSubtract(LoadFloat(instance.restVertices[edge.b].position), frame.center);
        const SIMDVector tangent = frame.tangent;
        const SIMDVector bitangent = frame.bitangent;
        const f32 ax = VectorGetX(Vector3Dot(aOffset, tangent));
        const f32 ay = VectorGetX(Vector3Dot(aOffset, bitangent));
        const f32 bx = VectorGetX(Vector3Dot(bOffset, tangent));
        const f32 by = VectorGetX(Vector3Dot(bOffset, bitangent));
        signedArea += (ax * by) - (bx * ay);
    }
    return signedArea * 0.5f;
}

template<typename EdgeVector>
void ReverseBoundaryLoop(EdgeVector& edges){
    if(edges.empty())
        return;

    usize left = 0u;
    usize right = edges.size() - 1u;
    while(left < right){
        EdgeRecord leftEdge = edges[left];
        EdgeRecord rightEdge = edges[right];
        const u32 leftA = leftEdge.a;
        const u32 rightA = rightEdge.a;
        leftEdge.a = leftEdge.b;
        leftEdge.b = leftA;
        rightEdge.a = rightEdge.b;
        rightEdge.b = rightA;
        edges[left] = rightEdge;
        edges[right] = leftEdge;
        ++left;
        --right;
    }
    if(left == right){
        const u32 a = edges[left].a;
        edges[left].a = edges[left].b;
        edges[left].b = a;
    }
}

template<typename EdgeVector>
void CanonicalizeBoundaryLoopStart(EdgeVector& edges){
    if(edges.empty())
        return;

    usize startEdgeIndex = 0u;
    for(usize edgeIndex = 1u; edgeIndex < edges.size(); ++edgeIndex){
        const EdgeRecord& edge = edges[edgeIndex];
        const EdgeRecord& startEdge = edges[startEdgeIndex];
        if(edge.a < startEdge.a || (edge.a == startEdge.a && edge.b < startEdge.b))
            startEdgeIndex = edgeIndex;
    }
    if(startEdgeIndex == 0u)
        return;

    Rotate(edges.begin(), edges.begin() + static_cast<isize>(startEdgeIndex), edges.end());
}

template<typename BoundaryEdgeVector, typename OrderedEdgeVector>
[[nodiscard]] bool BuildOrderedBoundaryLoop(
    const BoundaryEdgeVector& boundaryEdges,
    const DeformableRuntimeMeshInstance& instance,
    const HoleFrame& frame,
    OrderedEdgeVector& outOrderedEdges)
{
    outOrderedEdges.clear();
    if(boundaryEdges.empty())
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> visitedEdges{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    visitedEdges.resize(boundaryEdges.size(), 0u);

    using BoundaryVertexEdgeMap = HashMap<
        u32,
        BoundaryVertexEdges,
        Hasher<u32>,
        EqualTo<u32>,
        Core::Alloc::ScratchAllocator<Pair<const u32, BoundaryVertexEdges>>
    >;
    BoundaryVertexEdgeMap vertexEdges(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        Core::Alloc::ScratchAllocator<Pair<const u32, BoundaryVertexEdges>>(scratchArena)
    );
    vertexEdges.reserve(boundaryEdges.size());
    for(usize edgeIndex = 0u; edgeIndex < boundaryEdges.size(); ++edgeIndex){
        const EdgeRecord& edge = boundaryEdges[edgeIndex];
        if(!RegisterBoundaryVertexEdge(vertexEdges, edge.a, edgeIndex)
            || !RegisterBoundaryVertexEdge(vertexEdges, edge.b, edgeIndex)
        )
            return false;
    }

    const u32 startVertex = boundaryEdges[0].a;
    u32 currentVertex = startVertex;
    outOrderedEdges.reserve(boundaryEdges.size());
    while(outOrderedEdges.size() < boundaryEdges.size()){
        usize nextEdgeIndex = Limit<usize>::s_Max;
        EdgeRecord nextEdge;
        const auto foundEdges = vertexEdges.find(currentVertex);
        if(foundEdges == vertexEdges.end())
            return false;

        const BoundaryVertexEdges& adjacentEdges = foundEdges.value();
        for(u32 adjacencyIndex = 0u; adjacencyIndex < adjacentEdges.count; ++adjacencyIndex){
            const usize edgeIndex = adjacentEdges.edgeIndices[adjacencyIndex];
            if(edgeIndex >= boundaryEdges.size() || visitedEdges[edgeIndex] != 0u)
                continue;

            const EdgeRecord& edge = boundaryEdges[edgeIndex];
            if(edge.a == currentVertex){
                nextEdgeIndex = edgeIndex;
                nextEdge = edge;
                break;
            }
            if(edge.b == currentVertex){
                nextEdgeIndex = edgeIndex;
                nextEdge = edge;
                nextEdge.a = edge.b;
                nextEdge.b = edge.a;
                break;
            }
        }

        if(nextEdgeIndex == Limit<usize>::s_Max)
            return false;

        visitedEdges[nextEdgeIndex] = 1u;
        outOrderedEdges.push_back(nextEdge);
        currentVertex = nextEdge.b;
        if(currentVertex == startVertex && outOrderedEdges.size() != boundaryEdges.size())
            return false;
    }

    if(currentVertex != startVertex)
        return false;

    const f32 signedArea = ProjectedSignedLoopArea(instance, frame, outOrderedEdges);
    if(!IsFinite(signedArea) || Abs(signedArea) <= s_FrameEpsilon)
        return false;
    if(signedArea < 0.0f)
        ReverseBoundaryLoop(outOrderedEdges);
    CanonicalizeBoundaryLoopStart(outOrderedEdges);
    return true;
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
    StoreFloat(
        VectorMultiplyAdd(LoadFloat(source.deltaPosition), VectorReplicate(weight), LoadFloat(target.deltaPosition)),
        &target.deltaPosition
    );
    StoreFloat(
        VectorMultiplyAdd(LoadFloat(source.deltaNormal), VectorReplicate(weight), LoadFloat(target.deltaNormal)),
        &target.deltaNormal
    );
    StoreFloat(
        VectorMultiplyAdd(LoadFloat(source.deltaTangent), VectorReplicate(weight), LoadFloat(target.deltaTangent)),
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
        || holeResult.editRevision != instance.editRevision
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
    outAttachment.editRevision = holeResult.editRevision;
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
        || attachment.editRevision != instance.editRevision
        || !__hidden_deformable_surface_edit::ValidateUploadedRuntimePayload(instance)
        || !__hidden_deformable_surface_edit::RuntimeMeshHasWallTrianglePairs(
            instance,
            attachment.firstWallVertex,
            attachment.wallVertexCount
        )
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

    const usize wallIndexCount = wallVertexCount * 6u;
    if(wallIndexCount > instance.indices.size())
        return false;

    SIMDVector rimCenter = VectorZero();
    SIMDVector innerCenter = VectorZero();
    SIMDVector firstRimPosition = VectorZero();
    const usize wallIndexBase = instance.indices.size() - wallIndexCount;
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
    usize binarySize = 0u;
    if(!__hidden_deformable_surface_edit::ComputeSurfaceEditStateBinarySize(
            static_cast<u64>(state.edits.size()),
            static_cast<u64>(state.accessories.size()),
            binarySize
        )
    )
        return false;

    if(!__hidden_deformable_surface_edit::ValidSurfaceEditState(state))
        return false;

    outBinary.reserve(binarySize);
    __hidden_deformable_surface_edit::SurfaceEditStateHeader header;
    header.editCount = static_cast<u64>(state.edits.size());
    header.accessoryCount = static_cast<u64>(state.accessories.size());
    AppendPOD(outBinary, header);
    for(const DeformableSurfaceEditRecord& record : state.edits)
        AppendPOD(outBinary, record);
    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        __hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinary binaryRecord;
        if(!__hidden_deformable_surface_edit::BuildAccessoryBinaryRecord(accessory, binaryRecord)){
            outBinary.clear();
            return false;
        }
        AppendPOD(outBinary, binaryRecord);
    }
    return outBinary.size() == binarySize;
}

bool DeserializeSurfaceEditState(
    const Core::Assets::AssetBytes& binary,
    DeformableSurfaceEditState& outState)
{
    outState = DeformableSurfaceEditState{};
    usize cursor = 0;
    __hidden_deformable_surface_edit::SurfaceEditStateHeader header;
    if(!ReadPOD(binary, cursor, header)
        || header.magic != __hidden_deformable_surface_edit::s_SurfaceEditStateMagic
        || header.version != __hidden_deformable_surface_edit::s_SurfaceEditStateVersion
    )
        return false;

    usize expectedSize = 0u;
    if(!__hidden_deformable_surface_edit::ComputeSurfaceEditStateBinarySize(
            header.editCount,
            header.accessoryCount,
            expectedSize
        )
        || expectedSize != binary.size()
    )
        return false;

    outState.edits.resize(static_cast<usize>(header.editCount));
    for(DeformableSurfaceEditRecord& record : outState.edits){
        if(!ReadPOD(binary, cursor, record) || !__hidden_deformable_surface_edit::ValidEditRecord(record)){
            outState = DeformableSurfaceEditState{};
            return false;
        }
    }

    outState.accessories.resize(static_cast<usize>(header.accessoryCount));
    for(DeformableAccessoryAttachmentRecord& accessory : outState.accessories){
        __hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinary binaryRecord;
        if(!ReadPOD(binary, cursor, binaryRecord)
            || !__hidden_deformable_surface_edit::BuildAccessoryRecord(binaryRecord, accessory)
        ){
            outState = DeformableSurfaceEditState{};
            return false;
        }
    }

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

bool CommitDeformableRestSpaceHole(
    DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    DeformableHoleEditResult* outResult)
{
    if(outResult)
        *outResult = DeformableHoleEditResult{};
    if(!__hidden_deformable_surface_edit::ValidateUploadedRuntimePayload(instance)
        || !__hidden_deformable_surface_edit::ValidateParams(instance, params)
    )
        return false;

    const usize triangleCount = instance.indices.size() / 3u;
    u32 hitTriangleIndices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, params.posedHit.triangle, hitTriangleIndices))
        return false;

    f32 hitBary[3] = {};
    if(!DeformableValidation::NormalizeSourceBarycentric(params.posedHit.bary.values, hitBary))
        return false;

    __hidden_deformable_surface_edit::HoleFrame frame;
    if(!__hidden_deformable_surface_edit::BuildHoleFrame(instance, hitTriangleIndices, hitBary, frame))
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

    u32 removedTriangleCount = 0;
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        u32 indices[3] = {};
        if(!DeformableRuntime::ValidateTriangleIndex(instance, static_cast<u32>(triangle), indices))
            return false;

        const bool selectedTriangle = triangle == static_cast<usize>(params.posedHit.triangle);
        if(selectedTriangle
            || __hidden_deformable_surface_edit::TriangleInsideFootprint(instance, frame, radiusX, radiusY, indices)
        ){
            removeTriangle[triangle] = 1u;
            ++removedTriangleCount;
        }
    }

    if(removedTriangleCount == 0u || removedTriangleCount >= triangleCount)
        return false;

    using EdgeRecord = __hidden_deformable_surface_edit::EdgeRecord;
    using EdgeRecordVector = Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>>;
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
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        u32 indices[3] = {};
        if(!DeformableRuntime::ValidateTriangleIndex(instance, static_cast<u32>(triangle), indices))
            return false;

        __hidden_deformable_surface_edit::RegisterFullEdge(edges, indices[0], indices[1]);
        __hidden_deformable_surface_edit::RegisterFullEdge(edges, indices[1], indices[2]);
        __hidden_deformable_surface_edit::RegisterFullEdge(edges, indices[2], indices[0]);
    }
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        if(removeTriangle[triangle] == 0u)
            continue;

        u32 indices[3] = {};
        if(!DeformableRuntime::ValidateTriangleIndex(instance, static_cast<u32>(triangle), indices))
            return false;

        if(!__hidden_deformable_surface_edit::RegisterRemovedEdge(edges, indices[0], indices[1])
            || !__hidden_deformable_surface_edit::RegisterRemovedEdge(edges, indices[1], indices[2])
            || !__hidden_deformable_surface_edit::RegisterRemovedEdge(edges, indices[2], indices[0])
        )
            return false;
    }

    EdgeRecordVector boundaryEdges{Core::Alloc::ScratchAllocator<EdgeRecord>(scratchArena)};
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
            __hidden_deformable_surface_edit::IncrementVertexDegree(boundaryDegrees, edge.a);
            __hidden_deformable_surface_edit::IncrementVertexDegree(boundaryDegrees, edge.b);
        }
    }
    if(boundaryEdges.empty())
        return false;
    for(const auto& [vertex, degree] : boundaryDegrees){
        static_cast<void>(vertex);
        if(degree != 2u)
            return false;
    }
    EdgeRecordVector orderedBoundaryEdges{Core::Alloc::ScratchAllocator<EdgeRecord>(scratchArena)};
    if(!__hidden_deformable_surface_edit::BuildOrderedBoundaryLoop(boundaryEdges, instance, frame, orderedBoundaryEdges))
        return false;

    Vector<DeformableVertexRest> newRestVertices = instance.restVertices;
    Vector<SkinInfluence4> newSkin = instance.skin;
    Vector<SourceSample> newSourceSamples = instance.sourceSamples;
    Vector<DeformableMorph> newMorphs = instance.morphs;
    Vector<u32> newIndices;
    u32 newSourceTriangleCount = instance.sourceTriangleCount;
    if(newSourceSamples.empty() || newSourceSamples.size() != instance.restVertices.size() || newSourceTriangleCount == 0u)
        return false;

    usize wallVertexCount = 0u;
    if(params.depth > DeformableRuntime::s_Epsilon){
        if(orderedBoundaryEdges.size() > Limit<usize>::s_Max / 6u)
            return false;

        wallVertexCount = orderedBoundaryEdges.size();
        if(wallVertexCount > Limit<usize>::s_Max - instance.restVertices.size())
            return false;
        if(instance.restVertices.size() + wallVertexCount > static_cast<usize>(Limit<u32>::s_Max))
            return false;
    }

    const usize removedIndexCount = static_cast<usize>(removedTriangleCount) * 3u;
    const usize wallIndexCount = params.depth > DeformableRuntime::s_Epsilon
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
    if(reservedVertexCount != instance.restVertices.size()){
        newRestVertices.reserve(reservedVertexCount);
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
    }

    u32 firstWallVertex = Limit<u32>::s_Max;
    u32 addedWallVertexCount = 0u;
    if(params.depth > DeformableRuntime::s_Epsilon){
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

            const __hidden_deformable_surface_edit::EdgeRecord& edge = orderedBoundaryEdges[edgeIndex];
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

        Vector<u32, Core::Alloc::ScratchAllocator<u32>> innerVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        innerVertices.resize(boundaryVertexCount, 0u);

        for(usize edgeIndex = 0; edgeIndex < boundaryVertexCount; ++edgeIndex){
            const usize previousEdgeIndex = edgeIndex == 0u ? boundaryVertexCount - 1u : edgeIndex - 1u;
            const __hidden_deformable_surface_edit::EdgeRecord& edge = orderedBoundaryEdges[edgeIndex];

            __hidden_deformable_surface_edit::WallVertexFrame vertexFrame;
            if(!__hidden_deformable_surface_edit::BuildWallVertexFrame(
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
            if(!__hidden_deformable_surface_edit::BuildBlendedVertexColor(
                    newRestVertices,
                    innerAttributeVertices,
                    __hidden_deformable_surface_edit::s_WallInnerInpaintWeights,
                    innerColor
                )
            )
                return false;

            SkinInfluence4 innerSkin;
            const SkinInfluence4* innerSkinPtr = nullptr;
            if(!newSkin.empty()){
                if(!__hidden_deformable_surface_edit::BuildBlendedSkinInfluence(
                        newSkin,
                        innerAttributeVertices,
                        __hidden_deformable_surface_edit::s_WallInnerInpaintWeights,
                        innerSkin
                    )
                )
                    return false;

                innerSkinPtr = &innerSkin;
            }

            if(!__hidden_deformable_surface_edit::AppendWallVertex(
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

        if(!__hidden_deformable_surface_edit::TransferWallMorphDeltas(
                newMorphs,
                orderedBoundaryEdges,
                innerVertices
            )
        )
            return false;

        for(usize edgeIndex = 0; edgeIndex < boundaryVertexCount; ++edgeIndex){
            const usize nextEdgeIndex = (edgeIndex + 1u) % boundaryVertexCount;
            const u32 rimA = orderedBoundaryEdges[edgeIndex].a;
            const u32 rimB = orderedBoundaryEdges[nextEdgeIndex].a;
            const u32 innerB = innerVertices[nextEdgeIndex];
            const u32 innerA = innerVertices[edgeIndex];

            newIndices.push_back(rimA);
            newIndices.push_back(rimB);
            newIndices.push_back(innerB);
            newIndices.push_back(rimA);
            newIndices.push_back(innerB);
            newIndices.push_back(innerA);
            addedTriangleCount += 2u;
        }
    }

    if(!DeformableValidation::ValidRuntimePayloadArrays(
            newRestVertices,
            newIndices,
            newSourceTriangleCount,
            newSkin,
            newSourceSamples,
            newMorphs
        )
    )
        return false;

    instance.restVertices = Move(newRestVertices);
    instance.indices = Move(newIndices);
    instance.sourceTriangleCount = newSourceTriangleCount;
    instance.skin = Move(newSkin);
    instance.sourceSamples = Move(newSourceSamples);
    instance.morphs = Move(newMorphs);
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

