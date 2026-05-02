// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_surface_edit.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_manager.h>
#include <core/ecs/entity.h>
#include <core/ecs/world.h>
#include <core/geometry/attribute_transfer.h>
#include <core/geometry/frame_math.h>
#include <core/geometry/mesh_topology.h>
#include <core/geometry/surface_patch_edit.h>
#include <core/geometry/tangent_frame_rebuild.h>
#include <impl/assets_geometry/deformable_geometry_validation.h>
#include <impl/assets_geometry/geometry_asset.h>
#include <impl/assets_material/material_asset.h>
#include <impl/ecs_deformable/deformable_runtime_helpers.h>
#include <impl/ecs_render/components.h>
#include <global/binary.h>
#include <logger/client/logger.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_surface_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace DeformableRuntime;
using EdgeRecord = Core::Geometry::MeshTopologyEdge;

static constexpr f32 s_WallInnerInpaintWeights[3] = { 0.25f, 0.5f, 0.25f };
static constexpr u32 s_SurfaceEditStateMagic = 0x53454631u; // SEF1
static constexpr u32 s_SurfaceEditStateVersion = 10u;
static constexpr u32 s_MinWallLoopVertexCount = 3u;
static constexpr u32 s_MaxWallLoopCutCount = 8u;
static constexpr f32 s_HolePreviewSurfaceOffsetMin = 0.001f;
static constexpr f32 s_HolePreviewSurfaceOffsetRadiusScale = 0.025f;
static constexpr f32 s_MinOperatorProfileWallScale = 0.05f;
static constexpr f32 s_SurfaceRemeshClipEpsilon = 0.00001f;
static constexpr f32 s_SurfaceRemeshAreaEpsilon = 0.0000001f;
static constexpr f32 s_SurfaceRemeshVertexMergeDistanceSq = 0.0000000001f;
static constexpr f32 s_SurfaceRemeshAttributeMergeDistanceSq = 0.00000001f;
static constexpr Float4U s_HolePreviewColor = Float4U(1.0f, 1.0f, 1.0f, 1.0f);

struct HoleFrame{
    SIMDVector center = VectorZero();
    SIMDVector normal = VectorZero();
    SIMDVector tangent = VectorZero();
    SIMDVector bitangent = VectorZero();
};

struct SurfaceRemeshClipPoint{
    Float2U local = Float2U(0.0f, 0.0f);
    f32 depth = 0.0f;
    f32 bary[3] = {};
    u32 originalVertex = Limit<u32>::s_Max;
};
static_assert(IsTriviallyCopyable_V<SurfaceRemeshClipPoint>, "SurfaceRemeshClipPoint must stay cheap to copy");

struct SurfaceRemeshLocalBounds{
    f32 minX = 0.0f;
    f32 minY = 0.0f;
    f32 maxX = 0.0f;
    f32 maxY = 0.0f;
};

struct SurfaceRemeshGeneratedVertex{
    u32 vertex = Limit<u32>::s_Max;
    u32 sourceTriangle = Limit<u32>::s_Max;
    u32 sourceVertices[3] = {};
    f32 bary[3] = {};
    Float2U local = Float2U(0.0f, 0.0f);
    f32 depth = 0.0f;
    Float3U position = Float3U(0.0f, 0.0f, 0.0f);
};

struct SurfaceRemeshTriangle{
    u32 indices[3] = {};
    u32 sourceTriangle = Limit<u32>::s_Max;
};

namespace HoleResultWallMode{
    enum Enum : u8{
        Optional = 0u,
        Required = 1u,
    };
};

namespace SurfaceRemeshClipSide{
    enum Enum : u8{
        Outside = 0u,
        Inside = 1u,
    };
};

namespace SurfaceRemeshDepthPlane{
    enum Enum : u8{
        Min = 0u,
        Max = 1u,
    };
};

namespace SurfaceRemeshAreaMode{
    enum Enum : u8{
        Projected2D = 0u,
        DepthAware = 1u,
    };
};

namespace SurfaceRemeshDirection{
    enum Enum : u8{
        Normal = 0u,
        Tangent = 1u,
    };
};

[[nodiscard]] bool FiniteVec3(const SIMDVector value){
    return DeformableValidation::FiniteVector(value, 0x7u);
}

[[nodiscard]] bool ValidOperatorUpVector(const Float3U& operatorUp){
    const SIMDVector up = LoadFloat(operatorUp);
    const f32 lengthSquared = VectorGetX(Vector3LengthSq(up));
    return
        DeformableValidation::FiniteVector(up, 0x7u)
        && IsFinite(lengthSquared)
        && lengthSquared > Core::Geometry::s_FrameDirectionEpsilon
    ;
}

void ResolveTangentBitangentVectors(
    const SIMDVector normalVector,
    const SIMDVector tangentVector,
    const SIMDVector fallbackTangent,
    SIMDVector& outTangentVector,
    SIMDVector& outBitangentVector
){
    outTangentVector = Core::Geometry::FrameResolveTangent(normalVector, tangentVector, fallbackTangent);
    outBitangentVector = Core::Geometry::FrameResolveBitangent(normalVector, outTangentVector, s_SIMDIdentityR1);
}

struct SurfaceEditStateHeaderBinary{
    u32 magic = s_SurfaceEditStateMagic;
    u32 version = s_SurfaceEditStateVersion;
    u64 editCount = 0;
    u64 accessoryCount = 0;
    u64 stringTableByteCount = 0;
};
static_assert(IsStandardLayout_V<SurfaceEditStateHeaderBinary>, "SurfaceEditStateHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SurfaceEditStateHeaderBinary>, "SurfaceEditStateHeaderBinary must stay binary-serializable");

struct SurfaceEditAccessoryRecordBinary{
    DeformableSurfaceEditId anchorEditId = 0;
    u32 firstWallVertex = Limit<u32>::s_Max;
    u32 wallVertexCount = 0;
    f32 normalOffset = 0.0f;
    f32 uniformScale = 1.0f;
    f32 wallLoopParameter = s_DeformableAccessoryCenteredWallLoopParameter;
    u32 geometryPathOffset = Limit<u32>::s_Max;
    u32 materialPathOffset = Limit<u32>::s_Max;
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
    const f32 (&bary)[3]
){
    SIMDVector position = VectorScale(LoadRestVertexPosition(instance.restVertices[indices[0]]), bary[0]);
    position = VectorMultiplyAdd(LoadRestVertexPosition(instance.restVertices[indices[1]]), VectorReplicate(bary[1]), position);
    position = VectorMultiplyAdd(LoadRestVertexPosition(instance.restVertices[indices[2]]), VectorReplicate(bary[2]), position);
    return position;
}

[[nodiscard]] bool BuildTriangleNormal(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&indices)[3],
    SIMDVector& outNormal
){
    if(
        indices[0u] >= instance.restVertices.size()
        || indices[1u] >= instance.restVertices.size()
        || indices[2u] >= instance.restVertices.size()
    )
        return false;

    const SIMDVector a = LoadRestVertexPosition(instance.restVertices[indices[0u]]);
    const SIMDVector b = LoadRestVertexPosition(instance.restVertices[indices[1u]]);
    const SIMDVector c = LoadRestVertexPosition(instance.restVertices[indices[2u]]);
    const SIMDVector rawNormal = Vector3Cross(VectorSubtract(b, a), VectorSubtract(c, a));
    const f32 rawNormalLengthSq = VectorGetX(Vector3LengthSq(rawNormal));
    if(!IsFinite(rawNormalLengthSq))
        return false;
    if(rawNormalLengthSq <= Core::Geometry::s_FrameDirectionEpsilon){
        SIMDVector blendedNormal = VectorAdd(
            VectorAdd(
                LoadRestVertexNormal(instance.restVertices[indices[0u]]),
                LoadRestVertexNormal(instance.restVertices[indices[1u]])
            ),
            LoadRestVertexNormal(instance.restVertices[indices[2u]])
        );
        outNormal = Core::Geometry::FrameNormalizeDirection(blendedNormal, s_SIMDIdentityR2);
        return FiniteVec3(outNormal);
    }

    outNormal = Core::Geometry::FrameNormalizeDirection(rawNormal, s_SIMDIdentityR2);
    return FiniteVec3(outNormal);
}

[[nodiscard]] bool BuildHoleFrame(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&triangleIndices)[3],
    const f32 (&bary)[3],
    const Float3U& operatorUp,
    HoleFrame& outFrame
){
    const SIMDVector a = LoadRestVertexPosition(instance.restVertices[triangleIndices[0]]);
    const SIMDVector b = LoadRestVertexPosition(instance.restVertices[triangleIndices[1]]);
    const SIMDVector c = LoadRestVertexPosition(instance.restVertices[triangleIndices[2]]);
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
    SIMDVector tangentVector = VectorScale(VectorSetW(LoadRestVertexTangent(vertex0), 0.0f), bary[0]);
    tangentVector = VectorMultiplyAdd(
        VectorSetW(LoadRestVertexTangent(vertex1), 0.0f),
        VectorReplicate(bary[1]),
        tangentVector
    );
    tangentVector = VectorMultiplyAdd(
        VectorSetW(LoadRestVertexTangent(vertex2), 0.0f),
        VectorReplicate(bary[2]),
        tangentVector
    );
    const SIMDVector fallbackTangent = Core::Geometry::FrameResolveTangent(
        outFrame.normal,
        tangentVector,
        edge0
    );
    const SIMDVector fallbackBitangent = Core::Geometry::FrameResolveBitangent(
        outFrame.normal,
        fallbackTangent,
        s_SIMDIdentityR1
    );
    const SIMDVector operatorBitangent = Core::Geometry::FrameResolveTangent(
        outFrame.normal,
        LoadFloat(operatorUp),
        fallbackBitangent
    );
    outFrame.tangent = Core::Geometry::FrameResolveTangent(
        outFrame.normal,
        Vector3Cross(operatorBitangent, outFrame.normal),
        fallbackTangent
    );
    outFrame.bitangent = Core::Geometry::FrameResolveBitangent(
        outFrame.normal,
        outFrame.tangent,
        operatorBitangent
    );
    return
        VectorGetX(Vector3LengthSq(outFrame.normal)) > Core::Geometry::s_FrameDirectionEpsilon
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
    return ValidRuntimeMeshPayloadArrays(instance);
}

[[nodiscard]] DeformableSurfaceEditPermission::Enum ResolveSurfaceEditPermission(const DeformableEditMaskFlags flags){
    if(!ValidDeformableEditMaskFlags(flags) || (flags & DeformableEditMaskFlag::Forbidden) != 0u)
        return DeformableSurfaceEditPermission::Forbidden;
    if((flags & (DeformableEditMaskFlag::Restricted | DeformableEditMaskFlag::RequiresRepair)) != 0u)
        return DeformableSurfaceEditPermission::Restricted;
    return DeformableSurfaceEditPermission::Allowed;
}

[[nodiscard]] bool MatchingSourceSample(const SourceSample& lhs, const SourceSample& rhs){
    return
        lhs.sourceTri == rhs.sourceTri
        && Abs(lhs.bary[0] - rhs.bary[0]) <= DeformableValidation::s_BarycentricSumEpsilon
        && Abs(lhs.bary[1] - rhs.bary[1]) <= DeformableValidation::s_BarycentricSumEpsilon
        && Abs(lhs.bary[2] - rhs.bary[2]) <= DeformableValidation::s_BarycentricSumEpsilon
    ;
}

[[nodiscard]] bool ExactF32(const f32 lhs, const f32 rhs){
    return NWB_MEMCMP(&lhs, &rhs, sizeof(lhs)) == 0;
}

[[nodiscard]] bool ExactFloat3(const Float3U& lhs, const Float3U& rhs){
    return
        ExactF32(lhs.x, rhs.x)
        && ExactF32(lhs.y, rhs.y)
        && ExactF32(lhs.z, rhs.z)
    ;
}

[[nodiscard]] bool ExactFloat4(const Float4& lhs, const Float4& rhs){
    return
        ExactF32(lhs.x, rhs.x)
        && ExactF32(lhs.y, rhs.y)
        && ExactF32(lhs.z, rhs.z)
        && ExactF32(lhs.w, rhs.w)
    ;
}

[[nodiscard]] bool ExactFloat2(const Float2U& lhs, const Float2U& rhs){
    return ExactF32(lhs.x, rhs.x) && ExactF32(lhs.y, rhs.y);
}

[[nodiscard]] bool ExactOperatorFootprint(
    const DeformableOperatorFootprint& lhs,
    const DeformableOperatorFootprint& rhs
){
    if(lhs.vertexCount != rhs.vertexCount)
        return false;
    for(u32 i = 0u; i < lhs.vertexCount; ++i){
        if(!ExactFloat2(lhs.vertices[i], rhs.vertices[i]))
            return false;
    }
    return true;
}

[[nodiscard]] bool ExactOperatorProfile(
    const DeformableOperatorProfile& lhs,
    const DeformableOperatorProfile& rhs
){
    if(lhs.sampleCount != rhs.sampleCount)
        return false;
    for(u32 i = 0u; i < lhs.sampleCount; ++i){
        if(
            !ExactF32(lhs.samples[i].depth, rhs.samples[i].depth)
            || !ExactF32(lhs.samples[i].scale, rhs.samples[i].scale)
            || !ExactFloat2(lhs.samples[i].center, rhs.samples[i].center)
        )
            return false;
    }
    return true;
}

[[nodiscard]] bool ExactSourceSample(const SourceSample& lhs, const SourceSample& rhs){
    return
        lhs.sourceTri == rhs.sourceTri
        && ExactF32(lhs.bary[0], rhs.bary[0])
        && ExactF32(lhs.bary[1], rhs.bary[1])
        && ExactF32(lhs.bary[2], rhs.bary[2])
    ;
}

[[nodiscard]] bool ExactPosedHit(const DeformablePosedHit& lhs, const DeformablePosedHit& rhs){
    return
        lhs.entity == rhs.entity
        && lhs.runtimeMesh == rhs.runtimeMesh
        && lhs.editRevision == rhs.editRevision
        && lhs.triangle == rhs.triangle
        && ExactF32(lhs.bary[0], rhs.bary[0])
        && ExactF32(lhs.bary[1], rhs.bary[1])
        && ExactF32(lhs.bary[2], rhs.bary[2])
        && ExactF32(lhs.distance(), rhs.distance())
        && ExactFloat4(lhs.position, rhs.position)
        && ExactFloat4(lhs.normal, rhs.normal)
        && lhs.editMaskFlags == rhs.editMaskFlags
        && ExactSourceSample(lhs.restSample, rhs.restSample)
    ;
}

[[nodiscard]] bool ExactHoleEditParams(const DeformableHoleEditParams& lhs, const DeformableHoleEditParams& rhs){
    return
        ExactPosedHit(lhs.posedHit, rhs.posedHit)
        && ExactF32(lhs.radius, rhs.radius)
        && ExactF32(lhs.ellipseRatio, rhs.ellipseRatio)
        && ExactF32(lhs.depth, rhs.depth)
        && ExactFloat3(lhs.operatorUp, rhs.operatorUp)
        && ExactOperatorFootprint(lhs.operatorFootprint, rhs.operatorFootprint)
        && ExactOperatorProfile(lhs.operatorProfile, rhs.operatorProfile)
    ;
}

[[nodiscard]] bool ValidateHitRestSample(const DeformableRuntimeMeshInstance& instance, const DeformablePosedHit& hit){
    SourceSample resolvedSample{};
    if(!ResolveDeformableRestSurfaceSample(instance, hit.triangle, hit.bary, resolvedSample))
        return false;

    return MatchingSourceSample(hit.restSample, resolvedSample);
}

[[nodiscard]] bool ValidatePosedHitFrame(const DeformablePosedHit& hit){
    const SIMDVector position = hit.positionVector();
    const SIMDVector normal = hit.normalVector();
    const f32 normalLengthSquared = VectorGetX(Vector3LengthSq(normal));
    return
        IsFinite(hit.distance())
        && hit.distance() >= 0.0f
        && ExactF32(hit.position.w, 1.0f)
        && ExactF32(hit.normal.w, 0.0f)
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
    return
        IsFinite(radius)
        && IsFinite(ellipseRatio)
        && IsFinite(depth)
        && IsFinite(radius * ellipseRatio)
        && radius > s_Epsilon
        && (radius * ellipseRatio) > s_Epsilon
        && depth > s_Epsilon
    ;
}

[[nodiscard]] bool ValidateHoleShape(const DeformableHoleEditParams& params){
    return
        ValidateHoleShapeValues(params.radius, params.ellipseRatio, params.depth)
        && ValidOperatorUpVector(params.operatorUp)
        && ValidateHoleOperatorShape(params.operatorFootprint, params.operatorProfile)
    ;
}

[[nodiscard]] bool ValidateParams(const DeformableRuntimeMeshInstance& instance, const DeformableHoleEditParams& params){
    return
        ValidateHitIdentity(instance, params.posedHit)
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
    const DeformableSurfaceEditSession& session
){
    return
        session.active
        && session.entity == instance.entity
        && session.runtimeMesh == instance.handle
        && session.editRevision == instance.editRevision
        && ValidateHitIdentity(instance, session.hit)
    ;
}

[[nodiscard]] bool ValidateSurfaceEditSessionParams(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditSession& session,
    const DeformableHoleEditParams& params
){
    return
        ValidateSurfaceEditSession(instance, session)
        && ValidateParams(instance, params)
        && ExactPosedHit(session.hit, params.posedHit)
    ;
}

[[nodiscard]] bool ValidatePreviewedSurfaceEditSessionParams(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditSession& session,
    const DeformableHoleEditParams& params
){
    return
        ValidateSurfaceEditSessionParams(instance, session, params)
        && session.previewed
        && ExactHoleEditParams(session.previewParams, params)
    ;
}

[[nodiscard]] bool BuildPreviewFrame(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    HoleFrame& outFrame
){
    u32 hitTriangleIndices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, params.posedHit.triangle, hitTriangleIndices))
        return false;

    f32 hitBary[3] = {};
    if(!DeformableValidation::NormalizeSourceBarycentric(params.posedHit.bary.values, hitBary))
        return false;
    return BuildHoleFrame(instance, hitTriangleIndices, hitBary, params.operatorUp, outFrame);
}

[[nodiscard]] bool ValidWallVertexSpan(const u32 firstVertex, const u32 vertexCount){
    return
        firstVertex != Limit<u32>::s_Max
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

[[nodiscard]] bool AccessoryUsesWallLoopParameter(const f32 parameter){
    return parameter != s_DeformableAccessoryCenteredWallLoopParameter;
}

[[nodiscard]] bool ValidAccessoryWallLoopParameter(const f32 parameter){
    return
        parameter == s_DeformableAccessoryCenteredWallLoopParameter
        || (IsFinite(parameter) && parameter >= 0.0f && parameter < 1.0f)
    ;
}

[[nodiscard]] bool ValidHoleEditResult(
    const DeformableHoleEditResult& result,
    const HoleResultWallMode::Enum wallMode
){
    if(result.editRevision == 0u || result.removedTriangleCount == 0u)
        return false;
    if(!ValidWallLoopCutCount(result.wallLoopCutCount))
        return false;
    if(!ValidOptionalWallVertexSpan(result.firstWallVertex, result.wallVertexCount))
        return false;
    if(result.wallVertexCount == 0u)
        return
            wallMode == HoleResultWallMode::Optional
            && result.addedVertexCount == 0u
            && result.addedTriangleCount == 0u
            && result.wallLoopCutCount == 0u
        ;
    const u32 wallBandCount = result.wallLoopCutCount + 1u;
    const u32 addedRingCount = wallBandCount;
    if(result.wallVertexCount > Limit<u32>::s_Max / addedRingCount)
        return false;
    const u32 expectedAddedVertexCount = result.wallVertexCount * addedRingCount;
    if(result.addedVertexCount != expectedAddedVertexCount)
        return false;
    if(result.wallVertexCount > Limit<u32>::s_Max / (2u * wallBandCount))
        return false;
    const u32 wallTriangleCount = result.wallVertexCount * 2u * wallBandCount;
    const u32 maxCapTriangleCount = result.wallVertexCount - 2u;
    if(wallTriangleCount > Limit<u32>::s_Max - maxCapTriangleCount)
        return false;
    if(
        result.addedTriangleCount <= wallTriangleCount
        || result.addedTriangleCount > wallTriangleCount + maxCapTriangleCount
    )
        return false;
    return true;
}

[[nodiscard]] bool RuntimeMeshWallTrianglePairsMatchAt(
    const DeformableRuntimeMeshInstance& instance,
    const usize indexBase,
    const usize firstWallVertex,
    const usize wallVertexCount
){
    for(usize pairIndex = 0u; pairIndex < wallVertexCount; ++pairIndex){
        const usize nextPairIndex = (pairIndex + 1u) % wallVertexCount;
        const u32 innerA = static_cast<u32>(firstWallVertex + pairIndex);
        const u32 innerB = static_cast<u32>(firstWallVertex + nextPairIndex);
        const usize pairIndexBase = indexBase + (pairIndex * 6u);
        const u32 rimA = instance.indices[pairIndexBase + 0u];
        const u32 rimB = instance.indices[pairIndexBase + 1u];

        if(
            rimA >= firstWallVertex
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
    usize* outIndexBase = nullptr
){
    if(outIndexBase)
        *outIndexBase = Limit<usize>::s_Max;
    if(!ValidWallVertexSpan(firstWallVertexValue, wallVertexCountValue))
        return false;

    const usize firstWallVertex = static_cast<usize>(firstWallVertexValue);
    const usize wallVertexCount = static_cast<usize>(wallVertexCountValue);
    if(
        firstWallVertex >= instance.restVertices.size()
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
    const DeformableHoleEditResult& result
){
    return
        ValidHoleEditResult(result, HoleResultWallMode::Required)
        && RuntimeMeshHasWallTrianglePairs(instance, result.firstWallVertex, result.wallVertexCount)
    ;
}

[[nodiscard]] bool ValidStoredRestFrame(const Float3U& position, const Float3U& normal){
    const SIMDVector positionVector = LoadFloat(position);
    const SIMDVector normalVector = LoadFloat(normal);
    const f32 normalLengthSquared = VectorGetX(Vector3LengthSq(normalVector));
    return
        DeformableValidation::FiniteVector(positionVector, 0x7u)
        && DeformableValidation::FiniteVector(normalVector, 0x7u)
        && Abs(normalLengthSquared - 1.0f) <= DeformableValidation::s_RestFrameUnitLengthSquaredEpsilon
    ;
}

[[nodiscard]] bool ValidHoleRecord(const DeformableSurfaceHoleEditRecord& record){
    return
        ValidateHoleShapeValues(record.radius, record.ellipseRatio, record.depth)
        && ValidOperatorUpVector(record.operatorUp)
        && ValidateHoleOperatorShape(record.operatorFootprint, record.operatorProfile)
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
    if(!ValidHoleEditResult(record.result, HoleResultWallMode::Required))
        return false;
    return true;
}

[[nodiscard]] bool ValidAccessoryAttachmentValues(
    const DeformableSurfaceEditId anchorEditId,
    const u32 firstWallVertex,
    const u32 wallVertexCount,
    const f32 normalOffset,
    const f32 uniformScale,
    const f32 wallLoopParameter
){
    return
        ValidSurfaceEditId(anchorEditId)
        && ValidWallVertexSpan(firstWallVertex, wallVertexCount)
        && IsFinite(normalOffset)
        && normalOffset >= 0.0f
        && IsFinite(uniformScale)
        && uniformScale > 0.0f
        && ValidAccessoryWallLoopParameter(wallLoopParameter)
    ;
}

[[nodiscard]] bool ValidAccessoryAttachment(const DeformableAccessoryAttachmentComponent& attachment){
    return
        attachment.targetEntity.valid()
        && attachment.runtimeMesh.valid()
        && ValidAccessoryAttachmentValues(
            attachment.anchorEditId,
            attachment.firstWallVertex,
            attachment.wallVertexCount,
            attachment.normalOffset(),
            attachment.uniformScale(),
            attachment.wallLoopParameter()
        )
    ;
}

[[nodiscard]] bool ValidAccessoryRecord(const DeformableAccessoryAttachmentRecord& record){
    return ValidAccessoryAttachmentValues(
            record.anchorEditId,
            record.firstWallVertex,
            record.wallVertexCount,
            record.normalOffset,
            record.uniformScale,
            record.wallLoopParameter
        )
        && record.geometry.valid()
        && record.material.valid()
        && (record.geometryVirtualPathText.empty() || Name(record.geometryVirtualPathText.view()) == record.geometry.name())
        && (record.materialVirtualPathText.empty() || Name(record.materialVirtualPathText.view()) == record.material.name())
    ;
}

[[nodiscard]] bool AccessoryRecordHasStableAssetPaths(const DeformableAccessoryAttachmentRecord& record){
    return
        !record.geometryVirtualPathText.empty()
        && !record.materialVirtualPathText.empty()
        && Name(record.geometryVirtualPathText.view()) == record.geometry.name()
        && Name(record.materialVirtualPathText.view()) == record.material.name()
    ;
}

[[nodiscard]] bool EditRecordMatchesAccessory(
    const DeformableSurfaceEditRecord& record,
    const DeformableAccessoryAttachmentRecord& accessory
){
    return
        record.editId == accessory.anchorEditId
        && record.type == DeformableSurfaceEditRecordType::Hole
        && record.result.firstWallVertex == accessory.firstWallVertex
        && record.result.wallVertexCount == accessory.wallVertexCount
    ;
}

[[nodiscard]] const DeformableSurfaceEditRecord* FindEditRecordById(
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId
){
    if(!ValidSurfaceEditId(editId))
        return nullptr;

    for(const DeformableSurfaceEditRecord& record : state.edits){
        if(record.editId == editId)
            return &record;
    }
    return nullptr;
}

using SurfaceEditRecordLookupPair = Pair<DeformableSurfaceEditId, const DeformableSurfaceEditRecord*>;
using SurfaceEditRecordLookup = HashMap<
    DeformableSurfaceEditId,
    const DeformableSurfaceEditRecord*,
    Hasher<DeformableSurfaceEditId>,
    EqualTo<DeformableSurfaceEditId>,
    Core::Alloc::ScratchAllocator<SurfaceEditRecordLookupPair>
>;

[[nodiscard]] const DeformableSurfaceEditRecord* FindEditRecordById(
    const SurfaceEditRecordLookup& records,
    const DeformableSurfaceEditId editId
){
    if(!ValidSurfaceEditId(editId))
        return nullptr;

    const auto it = records.find(editId);
    return it == records.end() ? nullptr : it.value();
}

[[nodiscard]] bool BuildSurfaceEditRecordLookup(
    const DeformableSurfaceEditState& state,
    SurfaceEditRecordLookup& outRecords
){
    outRecords.clear();
    outRecords.reserve(state.edits.size());

    u32 expectedBaseEditRevision = 0u;
    for(const DeformableSurfaceEditRecord& record : state.edits){
        if(!ValidEditRecord(record))
            return false;
        if(!outRecords.emplace(record.editId, &record).second)
            return false;
        if(record.hole.baseEditRevision != expectedBaseEditRevision)
            return false;

        expectedBaseEditRevision = record.result.editRevision;
    }
    return true;
}

[[nodiscard]] bool ValidateSurfaceEditAccessoryAnchors(
    const DeformableSurfaceEditState& state,
    const DeformableRuntimeMeshInstance* runtimeInstance
){
    Core::Alloc::ScratchArena<> scratchArena;
    SurfaceEditRecordLookup editRecords(
        0,
        Hasher<DeformableSurfaceEditId>(),
        EqualTo<DeformableSurfaceEditId>(),
        Core::Alloc::ScratchAllocator<SurfaceEditRecordLookupPair>(scratchArena)
    );
    if(!BuildSurfaceEditRecordLookup(state, editRecords))
        return false;

    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        const DeformableSurfaceEditRecord* anchorEdit = FindEditRecordById(editRecords, accessory.anchorEditId);
        if(
            !ValidAccessoryRecord(accessory)
            || !anchorEdit
            || !EditRecordMatchesAccessory(*anchorEdit, accessory)
            || (
                runtimeInstance
                && !RuntimeMeshHasWallTrianglePairs(*runtimeInstance, accessory.firstWallVertex, accessory.wallVertexCount)
            )
        )
            return false;
    }
    return true;
}

[[nodiscard]] bool ValidSurfaceEditState(const DeformableSurfaceEditState& state){
    return ValidateSurfaceEditAccessoryAnchors(state, nullptr);
}

[[nodiscard]] bool ValidateReplayAccessoryAnchors(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditState& state
){
    return ValidateSurfaceEditAccessoryAnchors(state, &instance);
}

[[nodiscard]] bool ReplayContextTargetsInstance(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditReplayContext& context
){
    return !context.targetEntity.valid() || context.targetEntity == instance.entity;
}

[[nodiscard]] bool ComputeTriangleBarycentric(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&indices)[3],
    const SIMDVector point,
    f32 (&outBary)[3]
){
    const SIMDVector a = LoadRestVertexPosition(instance.restVertices[indices[0]]);
    const SIMDVector b = LoadRestVertexPosition(instance.restVertices[indices[1]]);
    const SIMDVector c = LoadRestVertexPosition(instance.restVertices[indices[2]]);
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
    const Float3U& storedNormal
){
    const SIMDVector a = LoadRestVertexPosition(instance.restVertices[indices[0]]);
    const SIMDVector b = LoadRestVertexPosition(instance.restVertices[indices[1]]);
    const SIMDVector c = LoadRestVertexPosition(instance.restVertices[indices[2]]);
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
    DeformableHoleEditParams& outParams
){
    outParams = DeformableHoleEditParams{};
    if(
        record.type != DeformableSurfaceEditRecordType::Hole
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
    if(
        !ResolveDeformableRestSurfaceSample(instance, static_cast<u32>(triangle), bary, sample)
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
    outParams.operatorUp = record.hole.operatorUp;
    outParams.operatorFootprint = record.hole.operatorFootprint;
    outParams.operatorProfile = record.hole.operatorProfile;
    return ValidateParams(instance, outParams);
}

[[nodiscard]] bool ValidMoveTargetHoleRecord(const DeformableSurfaceHoleEditRecord& record){
    return
        record.restSample.sourceTri != Limit<u32>::s_Max
        && DeformableValidation::ValidSourceBarycentric(record.restSample.bary)
        && ValidStoredRestFrame(record.restPosition, record.restNormal)
    ;
}

[[nodiscard]] bool BuildMoveTargetHoleRecord(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePosedHit& hit,
    DeformableSurfaceHoleEditRecord& outRecord
){
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
    const DeformableSurfaceEditRecord& record
){
    return
        result.removedTriangleCount == record.result.removedTriangleCount
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
    const DeformableSurfaceEditRecord& record
){
    return
        result.removedTriangleCount == record.result.removedTriangleCount
        && result.addedVertexCount == record.result.addedVertexCount
        && result.addedTriangleCount == record.result.addedTriangleCount
        && result.wallVertexCount == record.result.wallVertexCount
        && result.wallLoopCutCount == record.result.wallLoopCutCount
    ;
}

[[nodiscard]] bool HoleResultChangesTopology(const DeformableHoleEditResult& result){
    return
        result.removedTriangleCount != 0u
        || result.addedTriangleCount != 0u
        || result.addedVertexCount != 0u
    ;
}

[[nodiscard]] bool ValidateReplayAccessories(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditReplayContext& context
){
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
    u32& outRestoredAccessoryCount
){
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
        attachment.setWallLoopParameter(accessory.wallLoopParameter);
        ++outRestoredAccessoryCount;
    }
}

[[nodiscard]] bool ValidateReplayAccessoryAssets(
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditReplayContext& context
){
    if(state.accessories.empty() || !context.assetManager)
        return true;

    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        UniquePtr<Core::Assets::IAsset> geometryAsset;
        if(
            !context.assetManager->loadSync(Geometry::AssetTypeName(), accessory.geometry.name(), geometryAsset)
            || !geometryAsset
            || geometryAsset->assetType() != Geometry::AssetTypeName()
        )
            return false;

        UniquePtr<Core::Assets::IAsset> materialAsset;
        if(
            !context.assetManager->loadSync(Material::AssetTypeName(), accessory.material.name(), materialAsset)
            || !materialAsset
            || materialAsset->assetType() != Material::AssetTypeName()
        )
            return false;
    }
    return true;
}

template<typename StringTable>
[[nodiscard]] bool BuildAccessoryBinaryRecord(
    const DeformableAccessoryAttachmentRecord& record,
    SurfaceEditAccessoryRecordBinary& outRecord,
    StringTable& stringTable
){
    outRecord = SurfaceEditAccessoryRecordBinary{};
    if(!ValidAccessoryRecord(record) || !AccessoryRecordHasStableAssetPaths(record))
        return false;

    outRecord.anchorEditId = record.anchorEditId;
    outRecord.firstWallVertex = record.firstWallVertex;
    outRecord.wallVertexCount = record.wallVertexCount;
    outRecord.normalOffset = record.normalOffset;
    outRecord.uniformScale = record.uniformScale;
    outRecord.wallLoopParameter = record.wallLoopParameter;
    return
        ::AppendStringTableText(stringTable, record.geometryVirtualPathText, outRecord.geometryPathOffset)
        && ::AppendStringTableText(stringTable, record.materialVirtualPathText, outRecord.materialPathOffset)
    ;
}

[[nodiscard]] bool BuildAccessoryRecord(
    const SurfaceEditAccessoryRecordBinary& binary,
    const Core::Assets::AssetBytes& rawBinary,
    const usize stringTableOffset,
    const usize stringTableByteCount,
    DeformableAccessoryAttachmentRecord& outRecord
){
    outRecord = DeformableAccessoryAttachmentRecord{};
    outRecord.anchorEditId = binary.anchorEditId;
    outRecord.firstWallVertex = binary.firstWallVertex;
    outRecord.wallVertexCount = binary.wallVertexCount;
    outRecord.normalOffset = binary.normalOffset;
    outRecord.uniformScale = binary.uniformScale;
    outRecord.wallLoopParameter = binary.wallLoopParameter;
    if(
        !::ReadStringTableText(
            rawBinary,
            stringTableOffset,
            stringTableByteCount,
            binary.geometryPathOffset,
            outRecord.geometryVirtualPathText
        )
        || !::ReadStringTableText(
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

[[nodiscard]] bool ComputeSurfaceEditStateBinarySize(
    const u64 editCount,
    const u64 accessoryCount,
    const u64 stringTableByteCount,
    usize& outSize
){
    outSize = sizeof(SurfaceEditStateHeaderBinary);
    if(editCount > static_cast<u64>(Limit<usize>::s_Max / sizeof(DeformableSurfaceEditRecord)))
        return false;
    if(accessoryCount > static_cast<u64>(Limit<usize>::s_Max / sizeof(SurfaceEditAccessoryRecordBinary)))
        return false;
    if(stringTableByteCount > static_cast<u64>(Limit<u32>::s_Max))
        return false;

    const usize editBytes = static_cast<usize>(editCount) * sizeof(DeformableSurfaceEditRecord);
    const usize accessoryBytes = static_cast<usize>(accessoryCount) * sizeof(SurfaceEditAccessoryRecordBinary);
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
    const f32 scale = Sqrt((1.0f + dot) * 2.0f);
    if(!IsFinite(scale) || scale <= Core::Geometry::s_FrameDirectionEpsilon)
        return s_SIMDIdentityR3;

    SIMDVector rotationVector = VectorScale(axis, 1.0f / scale);
    return VectorSetW(rotationVector, scale * 0.5f);
}

[[nodiscard]] SIMDVector NormalizeRotationQuaternionVector(
    const SIMDVector rotationVector,
    const SIMDVector fallbackNormalVector
){
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

void StoreAccessoryAttachmentTransform(
    const SIMDVector accessoryPosition,
    const SIMDVector tangent,
    const SIMDVector normal,
    const f32 uniformScale,
    Core::Scene::TransformComponent& outTransform
){
    StoreFloat(VectorSetW(accessoryPosition, 0.0f), &outTransform.position);
    StoreFloat(RotationFromFrame(tangent, normal), &outTransform.rotation);
    outTransform.scale = Float4(uniformScale, uniformScale, uniformScale);
}

[[nodiscard]] bool TryStoreAccessoryAttachmentFrameTransform(
    const SIMDVector anchorPosition,
    const SIMDVector innerPosition,
    const SIMDVector tangentReference,
    const f32 normalOffset,
    const f32 uniformScale,
    Core::Scene::TransformComponent& outTransform
){
    const SIMDVector normal = Core::Geometry::FrameNormalizeDirection(
        VectorSubtract(anchorPosition, innerPosition),
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
    );
    if(
        !FiniteVec3(normal)
        || VectorGetX(Vector3LengthSq(normal)) <= Core::Geometry::s_FrameDirectionEpsilon
    )
        return false;

    const SIMDVector accessoryPosition = VectorMultiplyAdd(
        normal,
        VectorReplicate(normalOffset),
        anchorPosition
    );
    if(!FiniteVec3(accessoryPosition))
        return false;

    const SIMDVector tangent = Core::Geometry::FrameResolveTangent(
        normal,
        tangentReference,
        Core::Geometry::FrameFallbackTangent(normal)
    );
    StoreAccessoryAttachmentTransform(accessoryPosition, tangent, normal, uniformScale, outTransform);
    return true;
}

[[nodiscard]] bool BuildMorphDeltaLookup(
    const DeformableMorph& morph,
    const usize sourceDeltaCount,
    MorphDeltaLookup& outLookup
){
    outLookup.clear();
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
    const u32 vertex
){
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
    const u32 outputVertex
){
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
    const VertexVector& innerVertices
){
    if(orderedBoundaryEdges.size() != innerVertices.size())
        return false;
    if(innerVertices.empty())
        return true;

    const usize maxAddedDeltaCount = innerVertices.size();
    Core::Alloc::ScratchArena<> scratchArena;
    MorphDeltaLookup lookup(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        Core::Alloc::ScratchAllocator<Pair<const u32, usize>>(scratchArena)
    );
    for(DeformableMorph& morph : morphs){
        const usize sourceDeltaCount = morph.deltas.size();
        if(
            morph.deltas.size() > static_cast<usize>(Limit<u32>::s_Max)
            || maxAddedDeltaCount > static_cast<usize>(Limit<u32>::s_Max) - morph.deltas.size()
        )
            return false;
        if(sourceDeltaCount == 0u)
            continue;

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

            if(
                !AppendBlendedMorphDelta(
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
    SkinInfluence4& outSkin
){
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
    Float4U& outColor
){
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

template<usize sourceCount>
[[nodiscard]] bool BuildBlendedVertexUv0(
    const Vector<DeformableVertexRest>& vertices,
    const u32 (&sourceVertices)[sourceCount],
    const f32 (&sourceWeights)[sourceCount],
    Float2U& outUv0
){
    static_assert(sourceCount > 0u, "uv transfer requires source samples");
    outUv0 = Float2U(0.0f, 0.0f);

    f32 u = 0.0f;
    f32 v = 0.0f;
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

        const Float2U& sourceUv0 = vertices[vertex].uv0;
        if(!IsFinite(sourceUv0.x) || !IsFinite(sourceUv0.y))
            return false;

        u += sourceUv0.x * sourceWeight;
        v += sourceUv0.y * sourceWeight;
        weightSum += sourceWeight;
        if(!IsFinite(u) || !IsFinite(v) || !IsFinite(weightSum))
            return false;
    }

    if(!DeformableValidation::ActiveWeight(weightSum))
        return false;

    const f32 invWeightSum = 1.0f / weightSum;
    outUv0 = Float2U(u * invWeightSum, v * invWeightSum);
    return IsFinite(outUv0.x) && IsFinite(outUv0.y);
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
    u32& outVertex
){
    if(sourceVertex >= vertices.size() || vertices.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return false;

    const bool appendSkin = !skin.empty();
    const bool appendSourceSample = !sourceSamples.empty();
    if(appendSkin && sourceVertex >= skin.size())
        return false;
    if(appendSkin && (!wallSkin || !DeformableValidation::ValidSkinInfluence(*wallSkin)))
        return false;
    if(appendSourceSample && sourceVertex >= sourceSamples.size())
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
    if(appendSkin)
        skin.push_back(*wallSkin);
    if(appendSourceSample)
        sourceSamples.push_back(wallSourceSample);
    return true;
}

void RestoreStableOriginalSurfaceAttributes(
    const Vector<DeformableVertexRest>& originalRestVertices,
    Vector<DeformableVertexRest>& editedRestVertices
){
    const usize stableVertexCount = Min(originalRestVertices.size(), editedRestVertices.size());
    for(usize vertexIndex = 0u; vertexIndex < stableVertexCount; ++vertexIndex){
        const DeformableVertexRest& original = originalRestVertices[vertexIndex];
        DeformableVertexRest& edited = editedRestVertices[vertexIndex];
        edited.normal = original.normal;
        edited.tangent = original.tangent;
        edited.uv0 = original.uv0;
        edited.color0 = original.color0;
    }
}

template<typename VertexAllocator, typename RestVertexAllocator, typename IndexAllocator>
[[nodiscard]] bool AppendBottomCapTriangles(
    const Vector<u32, VertexAllocator>& capVertices,
    const Vector<DeformableVertexRest, RestVertexAllocator>& restVertices,
    const SIMDVector tangent,
    const SIMDVector bitangent,
    Vector<u32, IndexAllocator>& outIndices,
    u32* outAddedTriangleCount
){
    if(outAddedTriangleCount)
        *outAddedTriangleCount = 0u;
    if(
        capVertices.size() < 3u
        || capVertices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || capVertices.size() - 2u > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>> capPositions{
        Core::Alloc::ScratchAllocator<Float3U>(scratchArena)
    };
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> localCapVertices{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> localCapIndices{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
    capPositions.reserve(capVertices.size());
    localCapVertices.reserve(capVertices.size());
    for(usize capVertexIndex = 0u; capVertexIndex < capVertices.size(); ++capVertexIndex){
        const u32 capVertex = capVertices[capVertexIndex];
        if(capVertex >= restVertices.size())
            return false;

        capPositions.push_back(restVertices[capVertex].position);
        localCapVertices.push_back(static_cast<u32>(capVertexIndex));
    }

    if(
        !Core::Geometry::AppendSurfacePatchCapTriangles(
            localCapVertices.data(),
            localCapVertices.size(),
            capPositions.data(),
            capPositions.size(),
            tangent,
            bitangent,
            localCapIndices,
            outAddedTriangleCount
        )
    )
        return false;

    if(localCapIndices.size() > Limit<usize>::s_Max - outIndices.size())
        return false;

    const usize indexOffset = outIndices.size();
    for(const u32 localCapIndex : localCapIndices){
        if(localCapIndex >= capVertices.size())
            return false;
    }

    outIndices.reserve(indexOffset + localCapIndices.size());
    for(const u32 localCapIndex : localCapIndices)
        outIndices.push_back(capVertices[localCapIndex]);
    return true;
}

[[nodiscard]] bool PointInsideOperatorCrossSection(
    const DeformableOperatorFootprint& operatorFootprint,
    const f32 x,
    const f32 y
){
    return PointInsideOperatorFootprint(operatorFootprint, x, y);
}

[[nodiscard]] bool ApplySourceContinuationUvsToWallVertexPlan(
    const Vector<DeformableVertexRest>& restVertices,
    Core::Geometry::SurfacePatchWallVertex* wallVertices,
    const usize wallVertexCount
){
    if(!wallVertices)
        return false;

    for(usize i = 0u; i < wallVertexCount; ++i){
        const u32 sourceVertex = wallVertices[i].sourceVertex;
        if(sourceVertex >= restVertices.size())
            return false;

        const Float2U& sourceUv0 = restVertices[sourceVertex].uv0;
        if(!IsFinite(sourceUv0.x) || !IsFinite(sourceUv0.y))
            return false;

        wallVertices[i].uv0 = sourceUv0;
    }
    return true;
}

[[nodiscard]] bool BlendDeepestWallRingCapFrames(
    Vector<DeformableVertexRest>& restVertices,
    const u32 firstWallVertex,
    const u32 wallVertexCount,
    const SIMDVector capNormal,
    const SIMDVector fallbackTangent
){
    if(!ValidWallVertexSpan(firstWallVertex, wallVertexCount))
        return false;

    const usize first = static_cast<usize>(firstWallVertex);
    const usize count = static_cast<usize>(wallVertexCount);
    if(first >= restVertices.size() || count > restVertices.size() - first)
        return false;

    const SIMDVector safeCapNormal = Core::Geometry::FrameNormalizeDirection(capNormal, s_SIMDIdentityR2);
    if(!FiniteVec3(safeCapNormal))
        return false;

    for(usize vertexOffset = 0u; vertexOffset < count; ++vertexOffset){
        DeformableVertexRest& vertex = restVertices[first + vertexOffset];
        const SIMDVector sideNormal = LoadRestVertexNormal(vertex);
        const SIMDVector normal = Core::Geometry::FrameNormalizeDirection(
            VectorAdd(sideNormal, safeCapNormal),
            safeCapNormal
        );
        const SIMDVector tangent = Core::Geometry::FrameResolveTangent(
            normal,
            VectorSetW(LoadRestVertexTangent(vertex), 0.0f),
            fallbackTangent
        );
        StoreFloat(normal, &vertex.normal);
        StoreFloat(
            VectorSetW(tangent, Core::Geometry::FrameTangentHandedness(vertex.tangent.w, 1.0f)),
            &vertex.tangent
        );
        if(!DeformableValidation::ValidRestVertexFrame(vertex))
            return false;
    }

    return true;
}

using SurfaceRemeshClipPolygon = Vector<
    SurfaceRemeshClipPoint,
    Core::Alloc::ScratchAllocator<SurfaceRemeshClipPoint>
>;
using SurfaceRemeshClipPolygonList = Vector<
    SurfaceRemeshClipPolygon,
    Core::Alloc::ScratchAllocator<SurfaceRemeshClipPolygon>
>;

[[nodiscard]] bool UseOperatorSurfaceRemesh(const DeformableHoleEditParams& params){
    return
        ValidateHoleOperatorShape(params.operatorFootprint, params.operatorProfile)
        && params.depth > DeformableRuntime::s_Epsilon
    ;
}

[[nodiscard]] f32 SurfaceRemeshHalfPlaneDistance(
    const Float2U& edgeA,
    const Float2U& edgeB,
    const SurfaceRemeshClipPoint& point,
    const f32 orientation
){
    const f32 edgeX = edgeB.x - edgeA.x;
    const f32 edgeY = edgeB.y - edgeA.y;
    const f32 pointX = point.local.x - edgeA.x;
    const f32 pointY = point.local.y - edgeA.y;
    return orientation * ((edgeX * pointY) - (edgeY * pointX));
}

[[nodiscard]] bool SurfaceRemeshKeepHalfPlanePoint(
    const f32 distance,
    const SurfaceRemeshClipSide::Enum clipSide
){
    return clipSide == SurfaceRemeshClipSide::Inside
        ? distance >= -s_SurfaceRemeshClipEpsilon
        : distance <= s_SurfaceRemeshClipEpsilon
    ;
}

[[nodiscard]] bool BuildOperatorFootprintLocalBounds(
    const DeformableOperatorFootprint& footprint,
    SurfaceRemeshLocalBounds& outBounds
){
    outBounds = SurfaceRemeshLocalBounds{};
    if(footprint.vertexCount == 0u || footprint.vertexCount > s_DeformableOperatorFootprintMaxVertexCount)
        return false;

    const Float2U& firstPoint = footprint.vertices[0u];
    if(!IsFinite(firstPoint.x) || !IsFinite(firstPoint.y))
        return false;

    outBounds.minX = firstPoint.x;
    outBounds.maxX = firstPoint.x;
    outBounds.minY = firstPoint.y;
    outBounds.maxY = firstPoint.y;
    for(u32 i = 1u; i < footprint.vertexCount; ++i){
        const Float2U& point = footprint.vertices[i];
        if(!IsFinite(point.x) || !IsFinite(point.y))
            return false;

        outBounds.minX = Min(outBounds.minX, point.x);
        outBounds.minY = Min(outBounds.minY, point.y);
        outBounds.maxX = Max(outBounds.maxX, point.x);
        outBounds.maxY = Max(outBounds.maxY, point.y);
    }
    return true;
}

[[nodiscard]] bool BuildSurfaceRemeshClipPointLocalBounds(
    const SurfaceRemeshClipPoint* points,
    const usize pointCount,
    SurfaceRemeshLocalBounds& outBounds
){
    outBounds = SurfaceRemeshLocalBounds{};
    if(!points || pointCount == 0u)
        return false;

    const Float2U& firstPoint = points[0u].local;
    if(!IsFinite(firstPoint.x) || !IsFinite(firstPoint.y))
        return false;

    outBounds.minX = firstPoint.x;
    outBounds.maxX = firstPoint.x;
    outBounds.minY = firstPoint.y;
    outBounds.maxY = firstPoint.y;
    for(usize i = 1u; i < pointCount; ++i){
        const Float2U& point = points[i].local;
        if(!IsFinite(point.x) || !IsFinite(point.y))
            return false;

        outBounds.minX = Min(outBounds.minX, point.x);
        outBounds.minY = Min(outBounds.minY, point.y);
        outBounds.maxX = Max(outBounds.maxX, point.x);
        outBounds.maxY = Max(outBounds.maxY, point.y);
    }
    return true;
}

[[nodiscard]] bool SurfaceRemeshLocalBoundsOverlap(
    const SurfaceRemeshLocalBounds& lhs,
    const SurfaceRemeshLocalBounds& rhs
){
    return
        lhs.maxX >= rhs.minX - s_SurfaceRemeshClipEpsilon
        && lhs.minX <= rhs.maxX + s_SurfaceRemeshClipEpsilon
        && lhs.maxY >= rhs.minY - s_SurfaceRemeshClipEpsilon
        && lhs.minY <= rhs.maxY + s_SurfaceRemeshClipEpsilon
    ;
}

[[nodiscard]] f32 SurfaceRemeshPointDistanceSq(
    const SurfaceRemeshClipPoint& lhs,
    const SurfaceRemeshClipPoint& rhs
){
    const f32 dx = rhs.local.x - lhs.local.x;
    const f32 dy = rhs.local.y - lhs.local.y;
    const f32 dz = rhs.depth - lhs.depth;
    return (dx * dx) + (dy * dy) + (dz * dz);
}

[[nodiscard]] bool NormalizeSurfaceRemeshClipPoint(SurfaceRemeshClipPoint& point){
    if(
        !IsFinite(point.local.x)
        || !IsFinite(point.local.y)
        || !IsFinite(point.depth)
        || !DeformableValidation::NormalizeSourceBarycentric(point.bary, point.bary)
    )
        return false;

    return true;
}

[[nodiscard]] bool AppendSurfaceRemeshClipPoint(
    SurfaceRemeshClipPolygon& polygon,
    SurfaceRemeshClipPoint point
){
    if(!NormalizeSurfaceRemeshClipPoint(point))
        return false;
    if(
        !polygon.empty()
        && SurfaceRemeshPointDistanceSq(polygon.back(), point) <= s_SurfaceRemeshVertexMergeDistanceSq
    )
        return true;

    polygon.push_back(point);
    return true;
}

[[nodiscard]] SurfaceRemeshClipPoint InterpolateSurfaceRemeshClipPoint(
    const SurfaceRemeshClipPoint& a,
    const SurfaceRemeshClipPoint& b,
    const f32 rawT
){
    const f32 t = Min(Max(rawT, 0.0f), 1.0f);
    SurfaceRemeshClipPoint point;
    point.local = Float2U(
        a.local.x + ((b.local.x - a.local.x) * t),
        a.local.y + ((b.local.y - a.local.y) * t)
    );
    point.depth = a.depth + ((b.depth - a.depth) * t);
    for(u32 i = 0u; i < 3u; ++i)
        point.bary[i] = a.bary[i] + ((b.bary[i] - a.bary[i]) * t);
    point.originalVertex = Limit<u32>::s_Max;
    return point;
}

template<typename DistanceFunc>
[[nodiscard]] bool ClipSurfaceRemeshPolygonByDistance(
    const SurfaceRemeshClipPolygon& input,
    const SurfaceRemeshClipSide::Enum clipSide,
    SurfaceRemeshClipPolygon& output,
    DistanceFunc&& distanceFunc
){
    output.clear();
    if(input.empty())
        return true;

    SurfaceRemeshClipPoint previous = input.back();
    f32 previousDistance = distanceFunc(previous);
    bool previousKept = SurfaceRemeshKeepHalfPlanePoint(previousDistance, clipSide);
    for(const SurfaceRemeshClipPoint& current : input){
        const f32 currentDistance = distanceFunc(current);
        const bool currentKept = SurfaceRemeshKeepHalfPlanePoint(currentDistance, clipSide);
        if(currentKept != previousKept){
            const f32 denominator = previousDistance - currentDistance;
            if(!IsFinite(denominator) || Abs(denominator) <= s_SurfaceRemeshClipEpsilon)
                return false;

            const f32 t = previousDistance / denominator;
            if(!AppendSurfaceRemeshClipPoint(output, InterpolateSurfaceRemeshClipPoint(previous, current, t)))
                return false;
        }
        if(currentKept && !AppendSurfaceRemeshClipPoint(output, current))
            return false;

        previous = current;
        previousDistance = currentDistance;
        previousKept = currentKept;
    }
    return true;
}

[[nodiscard]] bool ClipSurfaceRemeshPolygonHalfPlane(
    const SurfaceRemeshClipPolygon& input,
    const Float2U& edgeA,
    const Float2U& edgeB,
    const f32 orientation,
    const SurfaceRemeshClipSide::Enum clipSide,
    SurfaceRemeshClipPolygon& output
){
    return ClipSurfaceRemeshPolygonByDistance(
        input,
        clipSide,
        output,
        [&](const SurfaceRemeshClipPoint& point){
            return SurfaceRemeshHalfPlaneDistance(edgeA, edgeB, point, orientation);
        }
    );
}

[[nodiscard]] f32 SurfaceRemeshDepthPlaneDistance(
    const SurfaceRemeshClipPoint& point,
    const SurfaceRemeshDepthPlane::Enum depthPlane
){
    return depthPlane == SurfaceRemeshDepthPlane::Min ? point.depth : 1.0f - point.depth;
}

[[nodiscard]] bool ClipSurfaceRemeshPolygonDepthPlane(
    const SurfaceRemeshClipPolygon& input,
    const SurfaceRemeshDepthPlane::Enum depthPlane,
    const SurfaceRemeshClipSide::Enum clipSide,
    SurfaceRemeshClipPolygon& output
){
    return ClipSurfaceRemeshPolygonByDistance(
        input,
        clipSide,
        output,
        [&](const SurfaceRemeshClipPoint& point){
            return SurfaceRemeshDepthPlaneDistance(point, depthPlane);
        }
    );
}

[[nodiscard]] f32 SurfaceRemeshClipPointCrossLengthSq(
    const SurfaceRemeshClipPoint& a,
    const SurfaceRemeshClipPoint& b,
    const SurfaceRemeshClipPoint& c
){
    const f32 abX = b.local.x - a.local.x;
    const f32 abY = b.local.y - a.local.y;
    const f32 abZ = b.depth - a.depth;
    const f32 acX = c.local.x - a.local.x;
    const f32 acY = c.local.y - a.local.y;
    const f32 acZ = c.depth - a.depth;
    const f32 crossX = (abY * acZ) - (abZ * acY);
    const f32 crossY = (abZ * acX) - (abX * acZ);
    const f32 crossZ = (abX * acY) - (abY * acX);
    return (crossX * crossX) + (crossY * crossY) + (crossZ * crossZ);
}

[[nodiscard]] f32 SurfaceRemeshClipPointCross2D(
    const SurfaceRemeshClipPoint& a,
    const SurfaceRemeshClipPoint& b,
    const SurfaceRemeshClipPoint& c
){
    return
        ((b.local.x - a.local.x) * (c.local.y - a.local.y))
        - ((b.local.y - a.local.y) * (c.local.x - a.local.x))
    ;
}

[[nodiscard]] f32 SurfaceRemeshPolygonAreaLengthSq(const SurfaceRemeshClipPolygon& polygon){
    if(polygon.size() < 3u)
        return 0.0f;

    f32 areaX = 0.0f;
    f32 areaY = 0.0f;
    f32 areaZ = 0.0f;
    const SurfaceRemeshClipPoint& origin = polygon[0u];
    for(usize i = 1u; i + 1u < polygon.size(); ++i){
        const SurfaceRemeshClipPoint& b = polygon[i];
        const SurfaceRemeshClipPoint& c = polygon[i + 1u];
        const f32 abX = b.local.x - origin.local.x;
        const f32 abY = b.local.y - origin.local.y;
        const f32 abZ = b.depth - origin.depth;
        const f32 acX = c.local.x - origin.local.x;
        const f32 acY = c.local.y - origin.local.y;
        const f32 acZ = c.depth - origin.depth;
        areaX += (abY * acZ) - (abZ * acY);
        areaY += (abZ * acX) - (abX * acZ);
        areaZ += (abX * acY) - (abY * acX);
    }
    return (areaX * areaX) + (areaY * areaY) + (areaZ * areaZ);
}

[[nodiscard]] f32 SurfaceRemeshPolygonSignedArea2D(const SurfaceRemeshClipPolygon& polygon){
    if(polygon.size() < 3u)
        return 0.0f;

    f32 area = 0.0f;
    const usize vertexCount = polygon.size();
    for(usize i = 1u; i < vertexCount; ++i){
        const usize previous = i - 1u;
        area +=
            (polygon[previous].local.x * polygon[i].local.y)
            - (polygon[previous].local.y * polygon[i].local.x)
        ;
    }
    area +=
        (polygon[vertexCount - 1u].local.x * polygon[0u].local.y)
        - (polygon[vertexCount - 1u].local.y * polygon[0u].local.x)
    ;
    return area * 0.5f;
}

[[nodiscard]] bool SurfaceRemeshTriangleProjectionIsDegenerate(const SurfaceRemeshClipPoint (&points)[3]){
    return Abs(SurfaceRemeshClipPointCross2D(points[0u], points[1u], points[2u])) <= s_SurfaceRemeshAreaEpsilon;
}

[[nodiscard]] bool RemoveCollinearSurfaceRemeshClipPoints(
    SurfaceRemeshClipPolygon& polygon,
    const SurfaceRemeshAreaMode::Enum areaMode
){
    if(polygon.size() < 3u)
        return false;

    if(
        SurfaceRemeshPointDistanceSq(polygon.front(), polygon.back())
        <= s_SurfaceRemeshVertexMergeDistanceSq
    )
        polygon.pop_back();

    bool removed = true;
    while(removed && polygon.size() >= 3u){
        removed = false;
        for(usize i = 0u; i < polygon.size(); ++i){
            const usize previous = i == 0u ? polygon.size() - 1u : i - 1u;
            const usize next = (i + 1u) % polygon.size();
            const SurfaceRemeshClipPoint& a = polygon[previous];
            const SurfaceRemeshClipPoint& b = polygon[i];
            const SurfaceRemeshClipPoint& c = polygon[next];
            const f32 abDistanceSq = SurfaceRemeshPointDistanceSq(a, b);
            const f32 bcDistanceSq = SurfaceRemeshPointDistanceSq(b, c);
            if(
                abDistanceSq <= s_SurfaceRemeshVertexMergeDistanceSq
                || bcDistanceSq <= s_SurfaceRemeshVertexMergeDistanceSq
                || (
                    areaMode == SurfaceRemeshAreaMode::DepthAware
                        ? SurfaceRemeshClipPointCrossLengthSq(a, b, c)
                            <= s_SurfaceRemeshAreaEpsilon * s_SurfaceRemeshAreaEpsilon
                        : Abs(SurfaceRemeshClipPointCross2D(a, b, c)) <= s_SurfaceRemeshAreaEpsilon
                )
            ){
                polygon.erase(polygon.begin() + static_cast<ptrdiff_t>(i));
                removed = true;
                break;
            }
        }
    }

    return
        polygon.size() >= 3u
        && (
            areaMode == SurfaceRemeshAreaMode::DepthAware
                ? SurfaceRemeshPolygonAreaLengthSq(polygon) > s_SurfaceRemeshAreaEpsilon * s_SurfaceRemeshAreaEpsilon
                : Abs(SurfaceRemeshPolygonSignedArea2D(polygon)) > s_SurfaceRemeshAreaEpsilon
        )
    ;
}

[[nodiscard]] bool BuildSurfaceRemeshTriangleClipPoint(
    const DeformableRuntimeMeshInstance& instance,
    const HoleFrame& frame,
    const DeformableHoleEditParams& params,
    const u32 vertex,
    const u32 baryIndex,
    SurfaceRemeshClipPoint& outPoint
){
    if(vertex >= instance.restVertices.size() || baryIndex >= 3u)
        return false;

    const f32 radiusX = params.radius;
    const f32 radiusY = params.radius * params.ellipseRatio;
    if(radiusX <= s_Epsilon || radiusY <= s_Epsilon)
        return false;

    const SIMDVector offset = VectorSubtract(LoadRestVertexPosition(instance.restVertices[vertex]), frame.center);
    const f32 localX = VectorGetX(Vector3Dot(offset, frame.tangent)) / radiusX;
    const f32 localY = VectorGetX(Vector3Dot(offset, frame.bitangent)) / radiusY;
    const f32 normalizedDepth = -VectorGetX(Vector3Dot(offset, frame.normal)) / params.depth;
    if(!IsFinite(localX) || !IsFinite(localY) || !IsFinite(normalizedDepth))
        return false;

    outPoint = SurfaceRemeshClipPoint{};
    outPoint.local = Float2U(localX, localY);
    outPoint.depth = normalizedDepth;
    outPoint.bary[baryIndex] = 1.0f;
    outPoint.originalVertex = vertex;
    return true;
}

[[nodiscard]] SIMDVector SurfaceRemeshPointPosition(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&triangleIndices)[3],
    const SurfaceRemeshClipPoint& point
){
    return BarycentricPoint(instance, triangleIndices, point.bary);
}

[[nodiscard]] bool BlendSurfaceRemeshSourceDirection(
    const Vector<DeformableVertexRest>& vertices,
    const u32 (&sourceVertices)[3],
    const f32 (&sourceWeights)[3],
    const SurfaceRemeshDirection::Enum direction,
    SIMDVector& outDirection
){
    outDirection = VectorZero();
    for(u32 sourceIndex = 0u; sourceIndex < 3u; ++sourceIndex){
        const f32 sourceWeight = sourceWeights[sourceIndex];
        if(!IsFinite(sourceWeight) || sourceWeight < 0.0f)
            return false;
        if(!DeformableValidation::ActiveWeight(sourceWeight))
            continue;

        const u32 vertex = sourceVertices[sourceIndex];
        if(vertex >= vertices.size())
            return false;

        const DeformableVertexRest& source = vertices[vertex];
        const SIMDVector sourceDirection = direction == SurfaceRemeshDirection::Tangent
            ? VectorSetW(LoadRestVertexTangent(source), 0.0f)
            : LoadRestVertexNormal(source)
        ;
        outDirection = VectorMultiplyAdd(sourceDirection, VectorReplicate(sourceWeight), outDirection);
    }

    return FiniteVec3(outDirection);
}

[[nodiscard]] bool SurfaceRemeshAttributeSourcesMatch(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&lhsSourceVertices)[3],
    const f32 (&lhsSourceWeights)[3],
    const u32 (&rhsSourceVertices)[3],
    const f32 (&rhsSourceWeights)[3]
){
    Float2U lhsUv0;
    Float2U rhsUv0;
    if(
        !BuildBlendedVertexUv0(instance.restVertices, lhsSourceVertices, lhsSourceWeights, lhsUv0)
        || !BuildBlendedVertexUv0(instance.restVertices, rhsSourceVertices, rhsSourceWeights, rhsUv0)
    )
        return false;

    const f32 uvDx = rhsUv0.x - lhsUv0.x;
    const f32 uvDy = rhsUv0.y - lhsUv0.y;
    if((uvDx * uvDx) + (uvDy * uvDy) > s_SurfaceRemeshAttributeMergeDistanceSq)
        return false;

    SIMDVector lhsNormal;
    SIMDVector rhsNormal;
    if(
        !BlendSurfaceRemeshSourceDirection(
            instance.restVertices,
            lhsSourceVertices,
            lhsSourceWeights,
            SurfaceRemeshDirection::Normal,
            lhsNormal
        )
        || !BlendSurfaceRemeshSourceDirection(
            instance.restVertices,
            rhsSourceVertices,
            rhsSourceWeights,
            SurfaceRemeshDirection::Normal,
            rhsNormal
        )
    )
        return false;
    const f32 normalDistanceSq = VectorGetX(Vector3LengthSq(VectorSubtract(lhsNormal, rhsNormal)));
    if(!IsFinite(normalDistanceSq) || normalDistanceSq > s_SurfaceRemeshAttributeMergeDistanceSq)
        return false;

    SIMDVector lhsTangent;
    SIMDVector rhsTangent;
    if(
        !BlendSurfaceRemeshSourceDirection(
            instance.restVertices,
            lhsSourceVertices,
            lhsSourceWeights,
            SurfaceRemeshDirection::Tangent,
            lhsTangent
        )
        || !BlendSurfaceRemeshSourceDirection(
            instance.restVertices,
            rhsSourceVertices,
            rhsSourceWeights,
            SurfaceRemeshDirection::Tangent,
            rhsTangent
        )
    )
        return false;
    const f32 tangentDistanceSq = VectorGetX(Vector3LengthSq(VectorSubtract(lhsTangent, rhsTangent)));
    return IsFinite(tangentDistanceSq) && tangentDistanceSq <= s_SurfaceRemeshAttributeMergeDistanceSq;
}

[[nodiscard]] bool GetOrCreateSurfaceRemeshVertex(
    const DeformableRuntimeMeshInstance& instance,
    const u32 sourceTriangle,
    const u32 (&triangleIndices)[3],
    const SurfaceRemeshClipPoint& point,
    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& restPositions,
    Vector<SurfaceRemeshGeneratedVertex, Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>>& generatedVertices,
    u32& outVertex
){
    outVertex = Limit<u32>::s_Max;
    if(point.originalVertex != Limit<u32>::s_Max){
        if(point.originalVertex >= instance.restVertices.size())
            return false;
        outVertex = point.originalVertex;
        return true;
    }

    const SIMDVector position = SurfaceRemeshPointPosition(instance, triangleIndices, point);
    if(!FiniteVec3(position))
        return false;

    for(const u32 candidate : triangleIndices){
        if(candidate >= instance.restVertices.size())
            return false;

        const SIMDVector delta = VectorSubtract(position, LoadRestVertexPosition(instance.restVertices[candidate]));
        const f32 distanceSq = VectorGetX(Vector3LengthSq(delta));
        if(IsFinite(distanceSq) && distanceSq <= s_SurfaceRemeshVertexMergeDistanceSq){
            outVertex = candidate;
            return true;
        }
    }

    Float3U storedPosition;
    StoreFloat(position, &storedPosition);
    for(const SurfaceRemeshGeneratedVertex& generated : generatedVertices){
        const f32 localDx = generated.local.x - point.local.x;
        const f32 localDy = generated.local.y - point.local.y;
        const f32 localDz = generated.depth - point.depth;
        const SIMDVector generatedDelta = VectorSubtract(LoadFloat(generated.position), position);
        if(
            (localDx * localDx) + (localDy * localDy) + (localDz * localDz)
                <= s_SurfaceRemeshVertexMergeDistanceSq
            && VectorGetX(Vector3LengthSq(generatedDelta)) <= s_SurfaceRemeshVertexMergeDistanceSq
            && SurfaceRemeshAttributeSourcesMatch(
                instance,
                generated.sourceVertices,
                generated.bary,
                triangleIndices,
                point.bary
            )
        ){
            outVertex = generated.vertex;
            return true;
        }
    }

    if(restPositions.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return false;

    SurfaceRemeshGeneratedVertex generated;
    generated.vertex = static_cast<u32>(restPositions.size());
    generated.sourceTriangle = sourceTriangle;
    for(u32 i = 0u; i < 3u; ++i){
        generated.sourceVertices[i] = triangleIndices[i];
        generated.bary[i] = point.bary[i];
    }
    generated.local = point.local;
    generated.depth = point.depth;
    generated.position = storedPosition;
    generatedVertices.push_back(generated);
    restPositions.push_back(storedPosition);
    outVertex = generated.vertex;
    return true;
}

[[nodiscard]] bool AppendSurfaceRemeshTriangle(
    const Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& restPositions,
    const SIMDVector sourceNormal,
    const u32 a,
    const u32 b,
    const u32 c,
    const u32 sourceTriangle,
    Vector<SurfaceRemeshTriangle, Core::Alloc::ScratchAllocator<SurfaceRemeshTriangle>>& surfaceTriangles
){
    if(a == b || a == c || b == c || a >= restPositions.size() || b >= restPositions.size() || c >= restPositions.size())
        return true;

    const SIMDVector p0 = LoadFloat(restPositions[a]);
    const SIMDVector p1 = LoadFloat(restPositions[b]);
    const SIMDVector p2 = LoadFloat(restPositions[c]);
    const SIMDVector areaVector = Vector3Cross(VectorSubtract(p1, p0), VectorSubtract(p2, p0));
    const f32 areaLengthSq = VectorGetX(Vector3LengthSq(areaVector));
    if(!IsFinite(areaLengthSq))
        return false;
    if(areaLengthSq <= DeformableValidation::s_TriangleAreaLengthSquaredEpsilon)
        return true;

    SurfaceRemeshTriangle triangle;
    triangle.indices[0u] = a;
    if(VectorGetX(Vector3Dot(areaVector, sourceNormal)) >= 0.0f){
        triangle.indices[1u] = b;
        triangle.indices[2u] = c;
    }
    else{
        triangle.indices[1u] = c;
        triangle.indices[2u] = b;
    }
    triangle.sourceTriangle = sourceTriangle;
    surfaceTriangles.push_back(triangle);
    return true;
}

[[nodiscard]] bool AppendSurfaceRemeshPolygonTriangles(
    const DeformableRuntimeMeshInstance& instance,
    const u32 sourceTriangle,
    const u32 (&triangleIndices)[3],
    const SIMDVector sourceNormal,
    const SurfaceRemeshClipPolygon& polygon,
    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& restPositions,
    Vector<SurfaceRemeshGeneratedVertex, Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>>& generatedVertices,
    Vector<SurfaceRemeshTriangle, Core::Alloc::ScratchAllocator<SurfaceRemeshTriangle>>& surfaceTriangles,
    Core::Alloc::ScratchArena<>& scratchArena
){
    if(polygon.size() < 3u)
        return true;

    Vector<u32, Core::Alloc::ScratchAllocator<u32>> vertices{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
    vertices.reserve(polygon.size());
    for(usize pointIndex = 0u; pointIndex < polygon.size(); ++pointIndex){
        const SurfaceRemeshClipPoint& point = polygon[pointIndex];
        u32 vertex = Limit<u32>::s_Max;
        if(!GetOrCreateSurfaceRemeshVertex(instance, sourceTriangle, triangleIndices, point, restPositions, generatedVertices, vertex))
            return false;
        vertices.push_back(vertex);
    }

    for(usize i = 1u; i + 1u < vertices.size(); ++i){
        if(
            !AppendSurfaceRemeshTriangle(
                restPositions,
                sourceNormal,
                vertices[0u],
                vertices[i],
                vertices[i + 1u],
                sourceTriangle,
                surfaceTriangles
            )
        )
            return false;
    }
    return true;
}

[[nodiscard]] bool SurfaceRemeshEdgeContainsVertex(
    const Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& restPositions,
    const u32 edgeA,
    const u32 edgeB,
    const u32 vertex
){
    if(edgeA == edgeB || vertex == edgeA || vertex == edgeB)
        return false;
    if(edgeA >= restPositions.size() || edgeB >= restPositions.size() || vertex >= restPositions.size())
        return false;

    const SIMDVector a = LoadFloat(restPositions[edgeA]);
    const SIMDVector b = LoadFloat(restPositions[edgeB]);
    const SIMDVector p = LoadFloat(restPositions[vertex]);
    const SIMDVector ab = VectorSubtract(b, a);
    const f32 lengthSq = VectorGetX(Vector3LengthSq(ab));
    if(!IsFinite(lengthSq) || lengthSq <= s_SurfaceRemeshVertexMergeDistanceSq)
        return false;

    const f32 t = VectorGetX(Vector3Dot(VectorSubtract(p, a), ab)) / lengthSq;
    if(!IsFinite(t) || t <= s_SurfaceRemeshClipEpsilon || t >= 1.0f - s_SurfaceRemeshClipEpsilon)
        return false;

    const SIMDVector closest = VectorMultiplyAdd(ab, VectorReplicate(t), a);
    const f32 distanceSq = VectorGetX(Vector3LengthSq(VectorSubtract(p, closest)));
    return IsFinite(distanceSq) && distanceSq <= s_SurfaceRemeshVertexMergeDistanceSq;
}

[[nodiscard]] bool SurfaceRemeshTriangleAreaValid(
    const Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& restPositions,
    const u32 a,
    const u32 b,
    const u32 c
){
    if(a == b || a == c || b == c || a >= restPositions.size() || b >= restPositions.size() || c >= restPositions.size())
        return false;

    const SIMDVector p0 = LoadFloat(restPositions[a]);
    const SIMDVector p1 = LoadFloat(restPositions[b]);
    const SIMDVector p2 = LoadFloat(restPositions[c]);
    const f32 areaLengthSq = VectorGetX(Vector3LengthSq(Vector3Cross(VectorSubtract(p1, p0), VectorSubtract(p2, p0))));
    return IsFinite(areaLengthSq) && areaLengthSq > DeformableValidation::s_TriangleAreaLengthSquaredEpsilon;
}

[[nodiscard]] bool ResolveSurfaceRemeshTriangleDepthRange(
    const DeformableRuntimeMeshInstance& instance,
    const HoleFrame& frame,
    const DeformableHoleEditParams& params,
    const u32 (&triangleIndices)[3],
    f32& outMinDepth,
    f32& outMaxDepth
){
    outMinDepth = 0.0f;
    outMaxDepth = 0.0f;
    if(params.depth <= s_Epsilon)
        return false;

    bool foundDepth = false;
    for(const u32 vertex : triangleIndices){
        if(vertex >= instance.restVertices.size())
            return false;

        const SIMDVector offset = VectorSubtract(LoadRestVertexPosition(instance.restVertices[vertex]), frame.center);
        const f32 normalizedDepth = -VectorGetX(Vector3Dot(offset, frame.normal)) / params.depth;
        if(!IsFinite(normalizedDepth))
            return false;

        if(!foundDepth){
            outMinDepth = normalizedDepth;
            outMaxDepth = normalizedDepth;
            foundDepth = true;
        }
        else{
            outMinDepth = Min(outMinDepth, normalizedDepth);
            outMaxDepth = Max(outMaxDepth, normalizedDepth);
        }
    }

    return foundDepth;
}

[[nodiscard]] bool SurfaceRemeshTriangleDepthIntersectsOperator(
    const DeformableRuntimeMeshInstance& instance,
    const HoleFrame& frame,
    const DeformableHoleEditParams& params,
    const u32 (&triangleIndices)[3],
    bool& outIntersectsDepth
){
    outIntersectsDepth = false;

    f32 minDepth = 0.0f;
    f32 maxDepth = 0.0f;
    if(!ResolveSurfaceRemeshTriangleDepthRange(instance, frame, params, triangleIndices, minDepth, maxDepth))
        return false;

    outIntersectsDepth =
        maxDepth >= -s_DeformableOperatorProfileDepthEpsilon
        && minDepth <= 1.0f + s_DeformableOperatorProfileDepthEpsilon
    ;
    return true;
}

[[nodiscard]] bool GetOrCreateSurfaceRemeshTriangleSplitVertex(
    const DeformableRuntimeMeshInstance& instance,
    const u32 sourceTriangle,
    const SurfaceRemeshGeneratedVertex& generated,
    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& restPositions,
    Vector<SurfaceRemeshGeneratedVertex, Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>>& generatedVertices,
    u32& outVertex
){
    outVertex = Limit<u32>::s_Max;

    u32 sourceTriangleIndices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, sourceTriangle, sourceTriangleIndices))
        return false;

    SurfaceRemeshClipPoint point{};
    point.local = generated.local;
    point.depth = generated.depth;
    point.originalVertex = Limit<u32>::s_Max;
    if(!ComputeTriangleBarycentric(instance, sourceTriangleIndices, LoadFloat(generated.position), point.bary))
        return false;

    return GetOrCreateSurfaceRemeshVertex(
        instance,
        sourceTriangle,
        sourceTriangleIndices,
        point,
        restPositions,
        generatedVertices,
        outVertex
    );
}

[[nodiscard]] bool SplitSurfaceRemeshTrianglesAtGeneratedEdgeVertices(
    const DeformableRuntimeMeshInstance& instance,
    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& restPositions,
    Vector<SurfaceRemeshGeneratedVertex, Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>>& generatedVertices,
    Vector<SurfaceRemeshTriangle, Core::Alloc::ScratchAllocator<SurfaceRemeshTriangle>>& surfaceTriangles
){
    if(generatedVertices.empty() || surfaceTriangles.empty())
        return true;

    const usize generatedVertexScanCount = generatedVertices.size();
    if(generatedVertexScanCount <= Limit<usize>::s_Max - surfaceTriangles.size())
        surfaceTriangles.reserve(surfaceTriangles.size() + generatedVertexScanCount);

    bool splitTriangle = true;
    while(splitTriangle){
        splitTriangle = false;
        const usize triangleCount = surfaceTriangles.size();
        for(usize triangleIndex = 0u; triangleIndex < triangleCount && !splitTriangle; ++triangleIndex){
            const SurfaceRemeshTriangle triangle = surfaceTriangles[triangleIndex];
            for(u32 edgeIndex = 0u; edgeIndex < 3u && !splitTriangle; ++edgeIndex){
                const u32 edgeA = triangle.indices[edgeIndex];
                const u32 edgeB = triangle.indices[(edgeIndex + 1u) % 3u];
                const u32 opposite = triangle.indices[(edgeIndex + 2u) % 3u];
                for(usize generatedIndex = 0u; generatedIndex < generatedVertexScanCount; ++generatedIndex){
                    const SurfaceRemeshGeneratedVertex generated = generatedVertices[generatedIndex];
                    if(!SurfaceRemeshEdgeContainsVertex(restPositions, edgeA, edgeB, generated.vertex))
                        continue;

                    u32 splitVertex = Limit<u32>::s_Max;
                    if(
                        !GetOrCreateSurfaceRemeshTriangleSplitVertex(
                            instance,
                            triangle.sourceTriangle,
                            generated,
                            restPositions,
                            generatedVertices,
                            splitVertex
                        )
                    )
                        return false;
                    if(
                        !SurfaceRemeshTriangleAreaValid(restPositions, opposite, edgeA, splitVertex)
                        || !SurfaceRemeshTriangleAreaValid(restPositions, opposite, splitVertex, edgeB)
                    )
                        return false;
                    if(surfaceTriangles.size() >= static_cast<usize>(Limit<u32>::s_Max))
                        return false;

                    SurfaceRemeshTriangle first = triangle;
                    first.indices[0u] = opposite;
                    first.indices[1u] = edgeA;
                    first.indices[2u] = splitVertex;

                    SurfaceRemeshTriangle second = triangle;
                    second.indices[0u] = opposite;
                    second.indices[1u] = splitVertex;
                    second.indices[2u] = edgeB;

                    surfaceTriangles[triangleIndex] = first;
                    surfaceTriangles.push_back(second);
                    splitTriangle = true;
                    break;
                }
            }
        }
    }

    return true;
}

[[nodiscard]] bool RegisterSurfaceRemeshBoundaryEdge(
    HashMap<
        u64,
        EdgeRecord,
        Hasher<u64>,
        EqualTo<u64>,
        Core::Alloc::ScratchAllocator<Pair<const u64, EdgeRecord>>
    >& boundaryEdges,
    const u32 a,
    const u32 b
){
    if(a == b)
        return true;

    const u32 lo = a < b ? a : b;
    const u32 hi = a < b ? b : a;
    const u64 edgeKey = (static_cast<u64>(lo) << 32u) | static_cast<u64>(hi);
    auto [it, inserted] = boundaryEdges.emplace(edgeKey, EdgeRecord{});
    EdgeRecord& edge = it.value();
    if(inserted){
        edge.a = a;
        edge.b = b;
    }
    if(edge.fullCount == Limit<u32>::s_Max)
        return false;

    ++edge.fullCount;
    edge.removedCount = 1u;
    return true;
}

[[nodiscard]] bool BuildOperatorSurfaceRemeshPlan(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    HoleFrame& outFrame,
    Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>>& outOrderedBoundaryEdges,
    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& outRestPositions,
    Vector<SurfaceRemeshTriangle, Core::Alloc::ScratchAllocator<SurfaceRemeshTriangle>>& outSurfaceTriangles,
    Vector<SurfaceRemeshGeneratedVertex, Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>>& outGeneratedVertices,
    Vector<u32, Core::Alloc::ScratchAllocator<u32>>& outAffectedTriangleIndices,
    u32& outAffectedTriangleCount,
    Core::Alloc::ScratchArena<>& scratchArena
){
    outFrame = HoleFrame{};
    outOrderedBoundaryEdges.clear();
    outRestPositions.clear();
    outSurfaceTriangles.clear();
    outGeneratedVertices.clear();
    outAffectedTriangleIndices.clear();
    outAffectedTriangleCount = 0u;

    if(!UseOperatorSurfaceRemesh(params))
        return false;

    const usize triangleCount = instance.indices.size() / 3u;
    if(triangleCount == 0u || triangleCount > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    u32 hitTriangleIndices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, params.posedHit.triangle, hitTriangleIndices))
        return false;

    f32 hitBary[3] = {};
    if(!DeformableValidation::NormalizeSourceBarycentric(params.posedHit.bary.values, hitBary))
        return false;
    if(!BuildHoleFrame(instance, hitTriangleIndices, hitBary, params.operatorUp, outFrame))
        return false;

    if(!PointInsideOperatorCrossSection(params.operatorFootprint, 0.0f, 0.0f))
        return false;

    outRestPositions.reserve(instance.restVertices.size());
    for(const DeformableVertexRest& restVertex : instance.restVertices)
        outRestPositions.push_back(restVertex.position);
    outSurfaceTriangles.reserve(triangleCount);

    using BoundaryEdgeMap = HashMap<
        u64,
        EdgeRecord,
        Hasher<u64>,
        EqualTo<u64>,
        Core::Alloc::ScratchAllocator<Pair<const u64, EdgeRecord>>
    >;
    BoundaryEdgeMap boundaryEdgeMap(
        0,
        Hasher<u64>(),
        EqualTo<u64>(),
        Core::Alloc::ScratchAllocator<Pair<const u64, EdgeRecord>>(scratchArena)
    );
    const usize boundaryEdgeReserve = Min(
        triangleCount * 3u,
        Max(static_cast<usize>(params.operatorFootprint.vertexCount) * 8u, static_cast<usize>(64u))
    );
    boundaryEdgeMap.reserve(boundaryEdgeReserve);

    if(!ValidateOperatorFootprint(params.operatorFootprint))
        return false;
    const f32 footprintArea = OperatorFootprintSignedArea(params.operatorFootprint);
    if(!IsFinite(footprintArea))
        return false;
    const f32 orientation = footprintArea >= 0.0f ? 1.0f : -1.0f;
    SurfaceRemeshLocalBounds footprintBounds;
    if(!BuildOperatorFootprintLocalBounds(params.operatorFootprint, footprintBounds))
        return false;

    bool hitTriangleAffected = false;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        const usize indexBase = triangle * 3u;
        const u32 triangleIndices[3] = {
            instance.indices[indexBase + 0u],
            instance.indices[indexBase + 1u],
            instance.indices[indexBase + 2u],
        };
        if(
            triangleIndices[0u] >= instance.restVertices.size()
            || triangleIndices[1u] >= instance.restVertices.size()
            || triangleIndices[2u] >= instance.restVertices.size()
        )
            return false;

        SIMDVector triangleNormal;
        if(!BuildTriangleNormal(instance, triangleIndices, triangleNormal))
            return false;

        const auto appendOriginalTriangle = [&](){
            return AppendSurfaceRemeshTriangle(
                outRestPositions,
                triangleNormal,
                triangleIndices[0u],
                triangleIndices[1u],
                triangleIndices[2u],
                static_cast<u32>(triangle),
                outSurfaceTriangles
            );
        };

        bool intersectsOperatorDepth = false;
        if(
            !SurfaceRemeshTriangleDepthIntersectsOperator(
                instance,
                outFrame,
                params,
                triangleIndices,
                intersectsOperatorDepth
            )
        )
            return false;
        if(!intersectsOperatorDepth){
            if(!appendOriginalTriangle())
                return false;
            continue;
        }

        SurfaceRemeshClipPoint triangleClipPoints[3] = {};
        for(u32 vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex){
            if(
                !BuildSurfaceRemeshTriangleClipPoint(
                    instance,
                    outFrame,
                    params,
                    triangleIndices[vertexIndex],
                    vertexIndex,
                    triangleClipPoints[vertexIndex]
                )
            )
                return false;
        }
        const SurfaceRemeshAreaMode::Enum areaMode = SurfaceRemeshTriangleProjectionIsDegenerate(triangleClipPoints)
            ? SurfaceRemeshAreaMode::DepthAware
            : SurfaceRemeshAreaMode::Projected2D
        ;

        SurfaceRemeshLocalBounds triangleBounds;
        if(!BuildSurfaceRemeshClipPointLocalBounds(triangleClipPoints, 3u, triangleBounds))
            return false;
        if(!SurfaceRemeshLocalBoundsOverlap(triangleBounds, footprintBounds)){
            if(!appendOriginalTriangle())
                return false;
            continue;
        }

        SurfaceRemeshClipPolygon active{
            Core::Alloc::ScratchAllocator<SurfaceRemeshClipPoint>(scratchArena)
        };
        active.reserve(8u);
        for(const SurfaceRemeshClipPoint& point : triangleClipPoints){
            if(!AppendSurfaceRemeshClipPoint(active, point))
                return false;
        }

        SurfaceRemeshClipPolygonList outsidePieces{
            Core::Alloc::ScratchAllocator<SurfaceRemeshClipPolygon>(scratchArena)
        };
        outsidePieces.reserve(params.operatorFootprint.vertexCount);
        SurfaceRemeshClipPolygon outside{
            Core::Alloc::ScratchAllocator<SurfaceRemeshClipPoint>(scratchArena)
        };
        SurfaceRemeshClipPolygon inside{
            Core::Alloc::ScratchAllocator<SurfaceRemeshClipPoint>(scratchArena)
        };
        outside.reserve(active.size() + 1u);
        inside.reserve(active.size() + 1u);
        bool clippedAway = false;
        for(u32 edgeIndex = 0u; edgeIndex < params.operatorFootprint.vertexCount; ++edgeIndex){
            const u32 nextEdgeIndex = (edgeIndex + 1u) % params.operatorFootprint.vertexCount;
            const Float2U& edgeA = params.operatorFootprint.vertices[edgeIndex];
            const Float2U& edgeB = params.operatorFootprint.vertices[nextEdgeIndex];

            outside.reserve(active.size() + 1u);
            inside.reserve(active.size() + 1u);
            if(
                !ClipSurfaceRemeshPolygonHalfPlane(
                    active,
                    edgeA,
                    edgeB,
                    orientation,
                    SurfaceRemeshClipSide::Outside,
                    outside
                )
                || !ClipSurfaceRemeshPolygonHalfPlane(
                    active,
                    edgeA,
                    edgeB,
                    orientation,
                    SurfaceRemeshClipSide::Inside,
                    inside
                )
            )
                return false;

            if(RemoveCollinearSurfaceRemeshClipPoints(outside, areaMode)){
                outsidePieces.emplace_back(Core::Alloc::ScratchAllocator<SurfaceRemeshClipPoint>(scratchArena));
                AssignTriviallyCopyableVector(outsidePieces.back(), outside);
            }

            if(!RemoveCollinearSurfaceRemeshClipPoints(inside, areaMode)){
                clippedAway = true;
                break;
            }

            AssignTriviallyCopyableVector(active, inside);
        }

        auto clipActiveByDepthPlane = [&](const SurfaceRemeshDepthPlane::Enum depthPlane){
            bool hasStrictOutside = false;
            for(const SurfaceRemeshClipPoint& point : active){
                if(SurfaceRemeshDepthPlaneDistance(point, depthPlane) < -s_SurfaceRemeshClipEpsilon){
                    hasStrictOutside = true;
                    break;
                }
            }

            outside.reserve(active.size() + 1u);
            inside.reserve(active.size() + 1u);
            if(
                !ClipSurfaceRemeshPolygonDepthPlane(
                    active,
                    depthPlane,
                    SurfaceRemeshClipSide::Outside,
                    outside
                )
                || !ClipSurfaceRemeshPolygonDepthPlane(
                    active,
                    depthPlane,
                    SurfaceRemeshClipSide::Inside,
                    inside
                )
            )
                return false;

            if(hasStrictOutside && RemoveCollinearSurfaceRemeshClipPoints(outside, SurfaceRemeshAreaMode::DepthAware)){
                outsidePieces.emplace_back(Core::Alloc::ScratchAllocator<SurfaceRemeshClipPoint>(scratchArena));
                AssignTriviallyCopyableVector(outsidePieces.back(), outside);
            }

            if(!RemoveCollinearSurfaceRemeshClipPoints(inside, SurfaceRemeshAreaMode::DepthAware)){
                clippedAway = true;
                return true;
            }

            AssignTriviallyCopyableVector(active, inside);
            return true;
        };
        if(
            areaMode == SurfaceRemeshAreaMode::DepthAware
            && !clippedAway
            && (
                !clipActiveByDepthPlane(SurfaceRemeshDepthPlane::Min)
                || (
                    !clippedAway
                    && !clipActiveByDepthPlane(SurfaceRemeshDepthPlane::Max)
                )
            )
        )
            return false;

        if(clippedAway || !RemoveCollinearSurfaceRemeshClipPoints(active, areaMode)){
            if(!appendOriginalTriangle())
                return false;
            continue;
        }

        const u32 affectedTriangleIndex = static_cast<u32>(triangle);
        outAffectedTriangleIndices.push_back(affectedTriangleIndex);
        ++outAffectedTriangleCount;
        hitTriangleAffected = hitTriangleAffected || affectedTriangleIndex == params.posedHit.triangle;

        Vector<u32, Core::Alloc::ScratchAllocator<u32>> insideVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        insideVertices.reserve(active.size());
        for(usize pointIndex = 0u; pointIndex < active.size(); ++pointIndex){
            const SurfaceRemeshClipPoint& point = active[pointIndex];
            u32 vertex = Limit<u32>::s_Max;
            if(
                !GetOrCreateSurfaceRemeshVertex(
                    instance,
                    static_cast<u32>(triangle),
                    triangleIndices,
                    point,
                    outRestPositions,
                    outGeneratedVertices,
                    vertex
                )
            )
                return false;
            insideVertices.push_back(vertex);
        }
        if(areaMode != SurfaceRemeshAreaMode::DepthAware){
            for(usize vertexIndex = 0u; vertexIndex < insideVertices.size(); ++vertexIndex){
                const usize nextVertexIndex = (vertexIndex + 1u) % insideVertices.size();
                if(!RegisterSurfaceRemeshBoundaryEdge(boundaryEdgeMap, insideVertices[vertexIndex], insideVertices[nextVertexIndex]))
                    return false;
            }
        }

        for(const SurfaceRemeshClipPolygon& outside : outsidePieces){
            if(
                !AppendSurfaceRemeshPolygonTriangles(
                    instance,
                    static_cast<u32>(triangle),
                    triangleIndices,
                    triangleNormal,
                    outside,
                    outRestPositions,
                    outGeneratedVertices,
                    outSurfaceTriangles,
                    scratchArena
                )
            )
                return false;
        }
    }

    if(
        !SplitSurfaceRemeshTrianglesAtGeneratedEdgeVertices(
            instance,
            outRestPositions,
            outGeneratedVertices,
            outSurfaceTriangles
        )
    )
        return false;

    if(
        outAffectedTriangleCount == 0u
        || outAffectedTriangleCount >= static_cast<u32>(triangleCount)
        || !hitTriangleAffected
        || outSurfaceTriangles.empty()
    )
        return false;

    Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>> boundaryEdges{
        Core::Alloc::ScratchAllocator<EdgeRecord>(scratchArena)
    };
    boundaryEdges.reserve(boundaryEdgeMap.size());
    for(const auto& [edgeKey, edge] : boundaryEdgeMap){
        static_cast<void>(edgeKey);
        if(edge.fullCount == 0u || edge.fullCount > 2u)
            return false;
        if(edge.fullCount == 1u)
            boundaryEdges.push_back(edge);
    }
    if(boundaryEdges.size() < s_MinWallLoopVertexCount)
        return false;

    Core::Geometry::MeshTopologyBoundaryLoopFrame topologyFrame;
    StoreFloat(outFrame.center, &topologyFrame.center);
    StoreFloat(outFrame.tangent, &topologyFrame.tangent);
    StoreFloat(outFrame.bitangent, &topologyFrame.bitangent);
    if(
        !Core::Geometry::BuildOrderedBoundaryLoop(
            boundaryEdges,
            outRestPositions,
            topologyFrame,
            outOrderedBoundaryEdges
        )
    )
        return false;

    return outOrderedBoundaryEdges.size() >= s_MinWallLoopVertexCount;
}

template<typename EdgeVector, typename PositionVector>
[[nodiscard]] bool ApplyOperatorProfileToWallVertexPlan(
    const EdgeVector& orderedBoundaryEdges,
    const PositionVector& restPositions,
    const HoleFrame& frame,
    const DeformableHoleEditParams& params,
    Core::Geometry::SurfacePatchWallVertex* wallVertices,
    const usize wallVertexCount
){
    if(
        !ValidateOperatorProfile(params.operatorProfile)
        || orderedBoundaryEdges.empty()
        || wallVertexCount == 0u
        || (wallVertexCount % orderedBoundaryEdges.size()) != 0u
        || !wallVertices
        || params.radius <= s_Epsilon
        || params.radius * params.ellipseRatio <= s_Epsilon
        || params.depth <= s_Epsilon
    )
        return false;

    const usize boundaryVertexCount = orderedBoundaryEdges.size();
    const usize wallBandCount = wallVertexCount / boundaryVertexCount;
    const f32 radiusX = params.radius;
    const f32 radiusY = params.radius * params.ellipseRatio;
    const Float2U topCenter = params.operatorProfile.samples[0u].center;

    for(usize ringIndex = 0u; ringIndex < wallBandCount; ++ringIndex){
        const f32 wallV = static_cast<f32>(ringIndex + 1u) / static_cast<f32>(wallBandCount);
        Float2U center;
        f32 scale = 1.0f;
        if(!SampleOperatorProfile(params.operatorProfile, wallV, center, scale))
            return false;
        scale = Max(scale, s_MinOperatorProfileWallScale);

        const usize vertexBase = ringIndex * boundaryVertexCount;
        for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
            const EdgeRecord& edge = orderedBoundaryEdges[edgeIndex];
            if(edge.a >= restPositions.size())
                return false;

            const SIMDVector sourcePosition = LoadFloat(restPositions[edge.a]);
            const SIMDVector sourceOffset = VectorSubtract(sourcePosition, frame.center);
            const f32 localX = VectorGetX(Vector3Dot(sourceOffset, frame.tangent)) / radiusX;
            const f32 localY = VectorGetX(Vector3Dot(sourceOffset, frame.bitangent)) / radiusY;
            if(!IsFinite(localX) || !IsFinite(localY))
                return false;

            const f32 profiledX = center.x + ((localX - topCenter.x) * scale);
            const f32 profiledY = center.y + ((localY - topCenter.y) * scale);
            SIMDVector position = frame.center;
            position = VectorMultiplyAdd(frame.tangent, VectorReplicate(profiledX * radiusX), position);
            position = VectorMultiplyAdd(frame.bitangent, VectorReplicate(profiledY * radiusY), position);
            position = VectorMultiplyAdd(frame.normal, VectorReplicate(-params.depth * wallV), position);
            if(!FiniteVec3(position))
                return false;

            StoreFloat(position, &wallVertices[vertexBase + edgeIndex].position);
        }
    }

    return true;
}

[[nodiscard]] bool BuildHolePreviewBoundaryPlan(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    HoleFrame& outFrame,
    Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>>& outOrderedBoundaryEdges,
    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& outRestPositions,
    u32& outRemovedTriangleCount,
    Core::Alloc::ScratchArena<>& scratchArena
){
    Vector<SurfaceRemeshTriangle, Core::Alloc::ScratchAllocator<SurfaceRemeshTriangle>> surfaceTriangles{
        Core::Alloc::ScratchAllocator<SurfaceRemeshTriangle>(scratchArena)
    };
    Vector<SurfaceRemeshGeneratedVertex, Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>> generatedVertices{
        Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>(scratchArena)
    };
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> affectedTriangleIndices{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
    return BuildOperatorSurfaceRemeshPlan(
        instance,
        params,
        outFrame,
        outOrderedBoundaryEdges,
        outRestPositions,
        surfaceTriangles,
        generatedVertices,
        affectedTriangleIndices,
        outRemovedTriangleCount,
        scratchArena
    );
}

struct HolePreviewPlan{
    HoleFrame frame;
    Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>> orderedBoundaryEdges;
    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>> restPositions;
    u32 removedTriangleCount = 0u;

    explicit HolePreviewPlan(Core::Alloc::ScratchArena<>& scratchArena)
        : orderedBoundaryEdges(Core::Alloc::ScratchAllocator<EdgeRecord>(scratchArena))
        , restPositions(Core::Alloc::ScratchAllocator<Float3U>(scratchArena))
    {}
};

[[nodiscard]] bool BuildHolePreviewPlan(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    Core::Alloc::ScratchArena<>& scratchArena,
    HolePreviewPlan& outPlan
){
    return BuildHolePreviewBoundaryPlan(
        instance,
        params,
        outPlan.frame,
        outPlan.orderedBoundaryEdges,
        outPlan.restPositions,
        outPlan.removedTriangleCount,
        scratchArena
    );
}

[[nodiscard]] DeformableHolePreviewMeshVertex MakeHolePreviewMeshVertex(
    const Float3U& position,
    const Float3U& normal,
    const Float4U& color
){
    DeformableHolePreviewMeshVertex vertex;
    vertex.position = position;
    vertex.normal = normal;
    vertex.color = color;
    return vertex;
}

[[nodiscard]] bool AppendHolePreviewCap(
    const Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& positions,
    const SIMDVector normal,
    const SIMDVector tangent,
    const SIMDVector bitangent,
    DeformableHolePreviewMesh& mesh,
    Core::Alloc::ScratchArena<>& scratchArena
){
    if(positions.size() < s_MinWallLoopVertexCount)
        return false;
    if(positions.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(mesh.vertices.size() > static_cast<usize>(Limit<u32>::s_Max) - positions.size())
        return false;

    Vector<u32, Core::Alloc::ScratchAllocator<u32>> capVertices{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
    capVertices.reserve(positions.size());
    for(usize i = 0u; i < positions.size(); ++i)
        capVertices.push_back(static_cast<u32>(i));

    Vector<u32, Core::Alloc::ScratchAllocator<u32>> capIndices{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
    if(
        !Core::Geometry::AppendSurfacePatchCapTriangles(
            capVertices.data(),
            capVertices.size(),
            positions.data(),
            positions.size(),
            tangent,
            bitangent,
            capIndices
        )
    )
        return false;

    const u32 vertexBase = static_cast<u32>(mesh.vertices.size());
    Float3U capNormal;
    StoreFloat(normal, &capNormal);
    const usize vertexBaseIndex = mesh.vertices.size();
    mesh.vertices.reserve(vertexBaseIndex + positions.size());
    for(usize positionIndex = 0u; positionIndex < positions.size(); ++positionIndex)
        mesh.vertices.push_back(MakeHolePreviewMeshVertex(
            positions[positionIndex],
            capNormal,
            s_HolePreviewColor
        ));

    if(
        mesh.indices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || capIndices.size() > static_cast<usize>(Limit<u32>::s_Max) - mesh.indices.size()
    )
        return false;
    for(const u32 capIndex : capIndices){
        if(capIndex >= capVertices.size())
            return false;
    }

    const usize indexBase = mesh.indices.size();
    mesh.indices.reserve(indexBase + capIndices.size());
    for(const u32 capIndex : capIndices)
        mesh.indices.push_back(vertexBase + capIndex);
    return true;
}

[[nodiscard]] bool BuildHolePreviewMeshFromPlan(
    const DeformableHoleEditParams& params,
    const HoleFrame& frame,
    const Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>>& orderedBoundaryEdges,
    const Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& restPositions,
    const u32 removedTriangleCount,
    DeformableHolePreviewMesh& outMesh,
    Core::Alloc::ScratchArena<>& scratchArena
){
    outMesh = DeformableHolePreviewMesh{};

    const usize boundaryVertexCount = orderedBoundaryEdges.size();
    if(
        boundaryVertexCount < s_MinWallLoopVertexCount
        || boundaryVertexCount > static_cast<usize>(Limit<u32>::s_Max / 4u)
    )
        return false;

    const bool addWall = params.depth > DeformableRuntime::s_Epsilon;
    const usize sideVertexCount = addWall ? boundaryVertexCount * 2u : 0u;
    const usize bottomCapVertexCount = addWall ? boundaryVertexCount : 0u;
    if(
        boundaryVertexCount > Limit<usize>::s_Max - sideVertexCount
        || boundaryVertexCount + sideVertexCount > Limit<usize>::s_Max - bottomCapVertexCount
    )
        return false;

    const usize vertexReserve = boundaryVertexCount + sideVertexCount + bottomCapVertexCount;
    outMesh.vertices.reserve(vertexReserve);

    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>> topCapPositions{
        Core::Alloc::ScratchAllocator<Float3U>(scratchArena)
    };
    topCapPositions.reserve(boundaryVertexCount);
    const f32 surfaceOffset = Max(
        s_HolePreviewSurfaceOffsetMin,
        params.radius * s_HolePreviewSurfaceOffsetRadiusScale
    );
    for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
        const EdgeRecord& edge = orderedBoundaryEdges[edgeIndex];
        if(edge.a >= restPositions.size())
            return false;

        Float3U topCapPosition;
        StoreFloat(
            VectorMultiplyAdd(
                frame.normal,
                VectorReplicate(surfaceOffset),
                LoadFloat(restPositions[edge.a])
            ),
            &topCapPosition
        );
        topCapPositions.push_back(topCapPosition);
    }

    if(!AppendHolePreviewCap(topCapPositions, frame.normal, frame.tangent, frame.bitangent, outMesh, scratchArena))
        return false;

    if(addWall){
        Core::Geometry::MeshTopologyBoundaryLoopFrame topologyFrame;
        StoreFloat(frame.center, &topologyFrame.center);
        StoreFloat(frame.tangent, &topologyFrame.tangent);
        StoreFloat(frame.bitangent, &topologyFrame.bitangent);

        Vector<
            Core::Geometry::SurfacePatchWallVertex,
            Core::Alloc::ScratchAllocator<Core::Geometry::SurfacePatchWallVertex>
        > wallVertexPlan{
            Core::Alloc::ScratchAllocator<Core::Geometry::SurfacePatchWallVertex>(scratchArena)
        };
        wallVertexPlan.resize(boundaryVertexCount);
        if(
            !Core::Geometry::BuildSurfacePatchWallVertices(
                orderedBoundaryEdges,
                restPositions,
                topologyFrame,
                frame.normal,
                params.depth,
                1u,
                wallVertexPlan.data(),
                wallVertexPlan.size()
            )
        )
            return false;
        if(
            !ApplyOperatorProfileToWallVertexPlan(
                orderedBoundaryEdges,
                restPositions,
                frame,
                params,
                wallVertexPlan.data(),
                wallVertexPlan.size()
            )
        )
            return false;

        Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>> bottomCapPositions{
            Core::Alloc::ScratchAllocator<Float3U>(scratchArena)
        };
        bottomCapPositions.reserve(boundaryVertexCount);
        const usize sideVertexBase = outMesh.vertices.size();
        outMesh.vertices.reserve(sideVertexBase + sideVertexCount);
        for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
            outMesh.vertices.push_back(MakeHolePreviewMeshVertex(
                topCapPositions[edgeIndex],
                wallVertexPlan[edgeIndex].normal,
                s_HolePreviewColor
            ));
            outMesh.vertices.push_back(MakeHolePreviewMeshVertex(
                wallVertexPlan[edgeIndex].position,
                wallVertexPlan[edgeIndex].normal,
                s_HolePreviewColor
            ));

            bottomCapPositions.push_back(wallVertexPlan[edgeIndex].position);
        }

        if(
            outMesh.indices.size() > static_cast<usize>(Limit<u32>::s_Max)
            || boundaryVertexCount > (static_cast<usize>(Limit<u32>::s_Max) - outMesh.indices.size()) / 6u
        )
            return false;

        const usize sideIndexBase = outMesh.indices.size();
        outMesh.indices.reserve(sideIndexBase + boundaryVertexCount * 6u);
        for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
            const usize nextEdgeIndex = (edgeIndex + 1u) % boundaryVertexCount;
            const u32 topVertex = static_cast<u32>(sideVertexBase + edgeIndex * 2u);
            const u32 nextTopVertex = static_cast<u32>(sideVertexBase + nextEdgeIndex * 2u);
            const u32 bottomVertex = topVertex + 1u;
            const u32 nextBottomVertex = nextTopVertex + 1u;
            outMesh.indices.push_back(topVertex);
            outMesh.indices.push_back(nextTopVertex);
            outMesh.indices.push_back(nextBottomVertex);
            outMesh.indices.push_back(topVertex);
            outMesh.indices.push_back(nextBottomVertex);
            outMesh.indices.push_back(bottomVertex);
        }

        if(!AppendHolePreviewCap(bottomCapPositions, frame.normal, frame.tangent, frame.bitangent, outMesh, scratchArena))
            return false;
    }

    outMesh.removedTriangleCount = removedTriangleCount;
    outMesh.wallVertexCount = static_cast<u32>(boundaryVertexCount);
    outMesh.valid = !outMesh.vertices.empty() && !outMesh.indices.empty();
    return outMesh.valid;
}

[[nodiscard]] bool BuildHolePreviewPlanAndMesh(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    Core::Alloc::ScratchArena<>& scratchArena,
    HolePreviewPlan& plan,
    DeformableHolePreviewMesh& outMesh
){
    if(!BuildHolePreviewPlan(instance, params, scratchArena, plan))
        return false;

    return BuildHolePreviewMeshFromPlan(
        params,
        plan.frame,
        plan.orderedBoundaryEdges,
        plan.restPositions,
        plan.removedTriangleCount,
        outMesh,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool CommitRemeshedHoleImpl(
    DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    bool requireUploadedRuntimePayload,
    u32 wallLoopCutCount,
    DeformableHoleEditResult* outResult
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





bool BeginSurfaceEdit(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePosedHit& hit,
    DeformableSurfaceEditSession& outSession
){
    outSession = DeformableSurfaceEditSession{};
    if(!__hidden_deformable_surface_edit::ValidateRuntimePayload(instance))
        return false;
    if(!GeometryClassAllowsRuntimeDeform(instance.geometryClass))
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
    DeformableHolePreview& outPreview
){
    outPreview = DeformableHolePreview{};
    session.previewParams = DeformableHoleEditParams{};
    session.previewed = false;
    if(
        !__hidden_deformable_surface_edit::ValidateRuntimePayload(instance)
        || !GeometryClassAllowsRuntimeDeform(instance.geometryClass)
        || !__hidden_deformable_surface_edit::ValidateSurfaceEditSessionParams(instance, session, params)
    )
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    __hidden_deformable_surface_edit::HolePreviewPlan plan(scratchArena);
    DeformableHolePreviewMesh previewMesh;
    if(
        !__hidden_deformable_surface_edit::BuildHolePreviewPlanAndMesh(
            instance,
            params,
            scratchArena,
            plan,
            previewMesh
        )
    )
        return false;

    StoreFloat(VectorSetW(plan.frame.center, 1.0f), &outPreview.center);
    StoreFloat(VectorSetW(plan.frame.normal, 0.0f), &outPreview.normal);
    StoreFloat(VectorSetW(plan.frame.tangent, 0.0f), &outPreview.tangent);
    StoreFloat(VectorSetW(plan.frame.bitangent, 0.0f), &outPreview.bitangent);
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

bool BuildHolePreviewMesh(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    DeformableHolePreviewMesh& outMesh
){
    outMesh = DeformableHolePreviewMesh{};
    if(
        !__hidden_deformable_surface_edit::ValidateRuntimePayload(instance)
        || !GeometryClassAllowsRuntimeDeform(instance.geometryClass)
        || !__hidden_deformable_surface_edit::ValidateParams(instance, params)
    )
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    __hidden_deformable_surface_edit::HolePreviewPlan plan(scratchArena);
    return __hidden_deformable_surface_edit::BuildHolePreviewPlanAndMesh(
        instance,
        params,
        scratchArena,
        plan,
        outMesh
    );
}

bool CommitHole(
    DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditSession& session,
    const DeformableHoleEditParams& params,
    DeformableHoleEditResult* outResult,
    DeformableSurfaceEditRecord* outRecord
){
    if(outResult)
        *outResult = DeformableHoleEditResult{};
    if(outRecord)
        *outRecord = DeformableSurfaceEditRecord{};

    const bool validRuntimePayload = __hidden_deformable_surface_edit::ValidateRuntimePayload(instance);
    if(!validRuntimePayload){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: commit failed before edit, runtime payload is invalid (entity={} runtime_mesh={} dirty_flags={} vertices={} triangles={})")
            , instance.entity.id
            , instance.handle.value
            , static_cast<u32>(instance.dirtyFlags)
            , instance.restVertices.size()
            , instance.indices.size() / 3u
        );
        return false;
    }
    if(!GeometryClassAllowsRuntimeDeform(instance.geometryClass)){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: commit failed before edit, geometry class is not runtime-deformable (entity={} runtime_mesh={} class={})")
            , instance.entity.id
            , instance.handle.value
            , StringConvert(GeometryClassText(instance.geometryClass))
        );
        return false;
    }
    if(!__hidden_deformable_surface_edit::ValidatePreviewedSurfaceEditSessionParams(instance, session, params)){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: commit failed before edit, preview session no longer matches (entity={} runtime_mesh={} revision={} session_revision={} previewed={})")
            , instance.entity.id
            , instance.handle.value
            , instance.editRevision
            , session.editRevision
            , session.previewed ? 1u : 0u
        );
        return false;
    }

    __hidden_deformable_surface_edit::HoleFrame recordFrame;
    if(outRecord && !__hidden_deformable_surface_edit::BuildPreviewFrame(instance, params, recordFrame)){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: commit failed before edit, preview frame could not be rebuilt (entity={} runtime_mesh={} triangle={})")
            , instance.entity.id
            , instance.handle.value
            , params.posedHit.triangle
        );
        return false;
    }

    DeformableHoleEditResult result;
    if(
        !__hidden_deformable_surface_edit::CommitRemeshedHoleImpl(
            instance,
            params,
            false,
            0u,
            &result
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: commit failed while applying rest-space hole (entity={} runtime_mesh={} triangle={} radius={} ellipse={} depth={})")
            , instance.entity.id
            , instance.handle.value
            , params.posedHit.triangle
            , params.radius
            , params.ellipseRatio
            , params.depth
        );
        return false;
    }

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
        outRecord->hole.operatorUp = params.operatorUp;
        outRecord->hole.operatorFootprint = params.operatorFootprint;
        outRecord->hole.operatorProfile = params.operatorProfile;
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
    DeformableAccessoryAttachmentComponent& outAttachment
){
    return AttachAccessoryAtWallLoopParameter(
        instance,
        holeResult,
        s_DeformableAccessoryCenteredWallLoopParameter,
        normalOffset,
        uniformScale,
        outAttachment
    );
}

bool AttachAccessoryAtWallLoopParameter(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditResult& holeResult,
    const f32 wallLoopParameter,
    const f32 normalOffset,
    const f32 uniformScale,
    DeformableAccessoryAttachmentComponent& outAttachment
){
    outAttachment = DeformableAccessoryAttachmentComponent{};
    if(
        !__hidden_deformable_surface_edit::ValidateUploadedRuntimePayload(instance)
        || holeResult.editRevision > instance.editRevision
        || !__hidden_deformable_surface_edit::ValidAccessoryAttachmentValues(
                holeResult.editRevision,
                holeResult.firstWallVertex,
                holeResult.wallVertexCount,
                normalOffset,
                uniformScale,
                wallLoopParameter
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
    outAttachment.setWallLoopParameter(wallLoopParameter);
    return true;
}

bool ResolveAccessoryAttachmentTransform(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    const DeformableAccessoryAttachmentComponent& attachment,
    Core::Scene::TransformComponent& outTransform
){
    if(
        !__hidden_deformable_surface_edit::ValidAccessoryAttachment(attachment)
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
    if(
        !__hidden_deformable_surface_edit::RuntimeMeshHasWallTrianglePairs(
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
        if(
            !__hidden_deformable_surface_edit::RuntimeMeshHasWallTrianglePairs(
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

    if(__hidden_deformable_surface_edit::AccessoryUsesWallLoopParameter(attachment.wallLoopParameter())){
        const f32 scaledLoopParameter = attachment.wallLoopParameter() * static_cast<f32>(wallVertexCount);
        usize pairIndex = static_cast<usize>(Floor(scaledLoopParameter));
        if(pairIndex >= wallVertexCount)
            pairIndex = wallVertexCount - 1u;
        const usize nextPairIndex = (pairIndex + 1u) % wallVertexCount;
        const f32 pairAlpha = scaledLoopParameter - static_cast<f32>(pairIndex);
        const usize outerPairIndexBase = outerWallIndexBase + (pairIndex * 6u);
        if(outerPairIndexBase + 1u >= instance.indices.size())
            return false;

        const usize rimAIndex = static_cast<usize>(instance.indices[outerPairIndexBase + 0u]);
        const usize rimBIndex = static_cast<usize>(instance.indices[outerPairIndexBase + 1u]);
        const usize innerAIndex = firstWallVertex + pairIndex;
        const usize innerBIndex = firstWallVertex + nextPairIndex;
        if(
            rimAIndex >= posedVertices.size()
            || rimBIndex >= posedVertices.size()
            || innerAIndex >= posedVertices.size()
            || innerBIndex >= posedVertices.size()
        )
            return false;

        const SIMDVector rimA = LoadRestVertexPosition(posedVertices[rimAIndex]);
        const SIMDVector rimB = LoadRestVertexPosition(posedVertices[rimBIndex]);
        const SIMDVector innerA = LoadRestVertexPosition(posedVertices[innerAIndex]);
        const SIMDVector innerB = LoadRestVertexPosition(posedVertices[innerBIndex]);
        const SIMDVector alpha = VectorReplicate(pairAlpha);
        const SIMDVector rimPosition = VectorMultiplyAdd(VectorSubtract(rimB, rimA), alpha, rimA);
        const SIMDVector innerPosition = VectorMultiplyAdd(VectorSubtract(innerB, innerA), alpha, innerA);
        return __hidden_deformable_surface_edit::TryStoreAccessoryAttachmentFrameTransform(
            rimPosition,
            innerPosition,
            VectorSubtract(rimB, rimA),
            attachment.normalOffset(),
            attachment.uniformScale(),
            outTransform
        );
    }

    SIMDVector rimCenter = VectorZero();
    SIMDVector innerCenter = VectorZero();
    SIMDVector firstRimPosition = VectorZero();
    for(usize pairIndex = 0u; pairIndex < wallVertexCount; ++pairIndex){
        const usize outerPairIndexBase = outerWallIndexBase + (pairIndex * 6u);
        const usize innerPairIndexBase = wallIndexBase + (pairIndex * 6u);
        const usize innerVertexIndex = firstWallVertex + pairIndex;
        if(
            outerPairIndexBase + 5u >= instance.indices.size()
            || innerPairIndexBase + 5u >= instance.indices.size()
        )
            return false;

        const usize rimVertexIndex = static_cast<usize>(instance.indices[outerPairIndexBase + 0u]);
        if(rimVertexIndex >= posedVertices.size() || innerVertexIndex >= posedVertices.size())
            return false;

        const SIMDVector rimPosition = LoadRestVertexPosition(posedVertices[rimVertexIndex]);
        if(pairIndex == 0u)
            firstRimPosition = rimPosition;
        rimCenter = VectorAdd(rimCenter, rimPosition);
        innerCenter = VectorAdd(innerCenter, LoadRestVertexPosition(posedVertices[innerVertexIndex]));
    }

    const f32 invWallVertexCount = 1.0f / static_cast<f32>(wallVertexCount);
    rimCenter = VectorScale(rimCenter, invWallVertexCount);
    innerCenter = VectorScale(innerCenter, invWallVertexCount);
    return __hidden_deformable_surface_edit::TryStoreAccessoryAttachmentFrameTransform(
        rimCenter,
        innerCenter,
        VectorSubtract(firstRimPosition, rimCenter),
        attachment.normalOffset(),
        attachment.uniformScale(),
        outTransform
    );
}

bool SerializeSurfaceEditState(const DeformableSurfaceEditState& state, Core::Assets::AssetBytes& outBinary){
    outBinary.clear();
    if(!__hidden_deformable_surface_edit::ValidSurfaceEditState(state))
        return false;

    using AccessoryRecord = __hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinary;
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<AccessoryRecord, Core::Alloc::ScratchAllocator<AccessoryRecord>> accessoryRecords{
        Core::Alloc::ScratchAllocator<AccessoryRecord>(scratchArena)
    };
    accessoryRecords.reserve(state.accessories.size());

    Vector<u8, Core::Alloc::ScratchAllocator<u8>> stringTable{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    usize stringTableReserveBytes = 0u;
    bool canReserveStringTable = true;
    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        canReserveStringTable = canReserveStringTable
            && ::AddStringTableTextReserveBytes(stringTableReserveBytes, accessory.geometryVirtualPathText)
            && ::AddStringTableTextReserveBytes(stringTableReserveBytes, accessory.materialVirtualPathText)
        ;
    }
    if(canReserveStringTable)
        stringTable.reserve(stringTableReserveBytes);

    for(const DeformableAccessoryAttachmentRecord& accessory : state.accessories){
        __hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinary binaryRecord;
        if(!__hidden_deformable_surface_edit::BuildAccessoryBinaryRecord(accessory, binaryRecord, stringTable)){
            outBinary.clear();
            return false;
        }
        accessoryRecords.push_back(binaryRecord);
    }

    usize binarySize = 0u;
    if(
        !__hidden_deformable_surface_edit::ComputeSurfaceEditStateBinarySize(
            static_cast<u64>(state.edits.size()),
            static_cast<u64>(state.accessories.size()),
            static_cast<u64>(stringTable.size()),
            binarySize
        )
    )
        return false;

    outBinary.reserve(binarySize);
    __hidden_deformable_surface_edit::SurfaceEditStateHeaderBinary header;
    header.editCount = static_cast<u64>(state.edits.size());
    header.accessoryCount = static_cast<u64>(state.accessories.size());
    header.stringTableByteCount = static_cast<u64>(stringTable.size());
    AppendPOD(outBinary, header);
    if(::AppendBinaryVectorPayload(outBinary, state.edits) != BinaryVectorPayloadFailure::None)
        return false;
    if(::AppendBinaryVectorPayload(outBinary, accessoryRecords) != BinaryVectorPayloadFailure::None)
        return false;
    if(::AppendBinaryVectorPayload(outBinary, stringTable) != BinaryVectorPayloadFailure::None)
        return false;
    return outBinary.size() == binarySize;
}

bool DeserializeSurfaceEditState(const Core::Assets::AssetBytes& binary, DeformableSurfaceEditState& outState){
    outState = DeformableSurfaceEditState{};
    usize cursor = 0;
    u32 magic = 0u;
    u32 version = 0u;
    if(
        !ReadPOD(binary, cursor, magic)
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
    if(
        !ReadPOD(binary, cursor, editCount)
        || !ReadPOD(binary, cursor, accessoryCount)
        || !ReadPOD(binary, cursor, stringTableByteCount)
        || !__hidden_deformable_surface_edit::ComputeSurfaceEditStateBinarySize(
                editCount,
                accessoryCount,
                stringTableByteCount,
                expectedSize
            )
        || expectedSize != binary.size()
    )
        return false;

    const usize editRecordCount = static_cast<usize>(editCount);
    outState.edits.clear();
    if(::ReadBinaryVectorPayload(binary, cursor, editCount, outState.edits) != BinaryVectorPayloadFailure::None){
        outState = DeformableSurfaceEditState{};
        return false;
    }
    for(usize i = 0u; i < editRecordCount; ++i){
        if(!__hidden_deformable_surface_edit::ValidEditRecord(outState.edits[i])){
            outState = DeformableSurfaceEditState{};
            return false;
        }
    }

    const usize accessoryRecordCount = static_cast<usize>(accessoryCount);
    using AccessoryRecord = __hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinary;
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<AccessoryRecord, Core::Alloc::ScratchAllocator<AccessoryRecord>> accessoryRecords{
        Core::Alloc::ScratchAllocator<AccessoryRecord>(scratchArena)
    };
    if(::ReadBinaryVectorPayload(binary, cursor, accessoryCount, accessoryRecords) != BinaryVectorPayloadFailure::None){
        outState = DeformableSurfaceEditState{};
        return false;
    }

    const usize stringTableOffset = cursor;
    outState.accessories.clear();
    outState.accessories.reserve(accessoryRecordCount);
    for(usize i = 0u; i < accessoryRecords.size(); ++i){
        DeformableAccessoryAttachmentRecord accessory;
        if(
            !__hidden_deformable_surface_edit::BuildAccessoryRecord(
                accessoryRecords[i],
                binary,
                stringTableOffset,
                static_cast<usize>(stringTableByteCount),
                accessory
            )
        ){
            outState = DeformableSurfaceEditState{};
            return false;
        }
        outState.accessories.push_back(Move(accessory));
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
    constexpr usize s_EditDebugLineReserve = 384u;
    constexpr usize s_AccessoryDebugLineReserve = 192u;
    usize debugDumpReserve = outDump.size();
    if(state.edits.size() <= (Limit<usize>::s_Max - debugDumpReserve) / s_EditDebugLineReserve){
        debugDumpReserve += state.edits.size() * s_EditDebugLineReserve;
        if(state.accessories.size() <= (Limit<usize>::s_Max - debugDumpReserve) / s_AccessoryDebugLineReserve){
            debugDumpReserve += state.accessories.size() * s_AccessoryDebugLineReserve;
            outDump.reserve(debugDumpReserve);
        }
    }
    for(usize i = 0u; i < state.edits.size(); ++i){
        const DeformableSurfaceEditRecord& record = state.edits[i];
        outDump += StringFormat(
            "edit[{}] id={} type=hole base_revision={} result_revision={} source_tri={} bary=({},{},{}) rest_position=({},{},{}) rest_normal=({},{},{}) operator_up=({},{},{}) radius={} ellipse={} depth={} operator_footprint_vertices={} operator_profile_samples={} loop_cuts={} wall_span=({},{}) removed_triangles={} added_vertices={} added_triangles={}\n",
            i,
            record.editId,
            record.hole.baseEditRevision,
            record.result.editRevision,
            record.hole.restSample.sourceTri,
            record.hole.restSample.bary[0],
            record.hole.restSample.bary[1],
            record.hole.restSample.bary[2],
            record.hole.restPosition.x,
            record.hole.restPosition.y,
            record.hole.restPosition.z,
            record.hole.restNormal.x,
            record.hole.restNormal.y,
            record.hole.restNormal.z,
            record.hole.operatorUp.x,
            record.hole.operatorUp.y,
            record.hole.operatorUp.z,
            record.hole.radius,
            record.hole.ellipseRatio,
            record.hole.depth,
            record.hole.operatorFootprint.vertexCount,
            record.hole.operatorProfile.sampleCount,
            record.hole.wallLoopCutCount,
            record.result.firstWallVertex,
            record.result.wallVertexCount,
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
            "accessory[{}] geometry={} material={} anchor_edit_id={} first_wall_vertex={} wall_vertex_count={} normal_offset={} uniform_scale={} wall_loop={}\n",
            i,
            geometryPath,
            materialPath,
            accessory.anchorEditId,
            accessory.firstWallVertex,
            accessory.wallVertexCount,
            accessory.normalOffset,
            accessory.uniformScale,
            accessory.wallLoopParameter
        );
    }
    return true;
}

namespace __hidden_deformable_surface_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize sourceCount>
[[nodiscard]] bool BuildBlendedSourceSample(
    const Vector<SourceSample>& sourceSamples,
    const u32 (&sourceVertices)[sourceCount],
    const f32 (&sourceWeights)[sourceCount],
    const SourceSample& fallbackSample,
    const u32 sourceTriangleCount,
    SourceSample& outSample
){
    static_assert(sourceCount > 0u, "source sample transfer requires source samples");
    outSample = SourceSample{};
    if(sourceSamples.empty() || sourceTriangleCount == 0u)
        return false;

    bool foundSource = false;
    bool sameSourceTriangle = true;
    u32 sourceTriangle = Limit<u32>::s_Max;
    f32 bary[3] = {};
    for(usize sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex){
        const f32 weight = sourceWeights[sourceIndex];
        if(!IsFinite(weight) || weight < 0.0f)
            return false;
        if(!DeformableValidation::ActiveWeight(weight))
            continue;

        const u32 vertex = sourceVertices[sourceIndex];
        if(vertex >= sourceSamples.size())
            return false;

        const SourceSample& sample = sourceSamples[vertex];
        if(!DeformableValidation::ValidSourceSample(sample, sourceTriangleCount))
            return false;
        if(!foundSource){
            sourceTriangle = sample.sourceTri;
            foundSource = true;
        }
        else if(sample.sourceTri != sourceTriangle)
            sameSourceTriangle = false;

        for(u32 baryIndex = 0u; baryIndex < 3u; ++baryIndex)
            bary[baryIndex] += sample.bary[baryIndex] * weight;
    }

    if(!foundSource)
        return false;

    if(!sameSourceTriangle){
        outSample = fallbackSample;
        return DeformableValidation::ValidSourceSample(outSample, sourceTriangleCount);
    }

    outSample.sourceTri = sourceTriangle;
    return DeformableValidation::NormalizeSourceBarycentric(bary, outSample.bary);
}

[[nodiscard]] bool AppendSurfaceRemeshGeneratedRestVertex(
    const DeformableRuntimeMeshInstance& instance,
    const SurfaceRemeshGeneratedVertex& generated,
    const SourceSample& fallbackSourceSample,
    const SIMDVector fallbackNormal,
    const SIMDVector fallbackTangent,
    Vector<DeformableVertexRest>& newRestVertices,
    Vector<SkinInfluence4>& newSkin,
    Vector<SourceSample>& newSourceSamples
){
    if(
        generated.vertex != newRestVertices.size()
        || generated.sourceTriangle == Limit<u32>::s_Max
        || generated.sourceTriangle >= instance.indices.size() / 3u
    )
        return false;

    DeformableVertexRest vertex;
    vertex.position = generated.position;

    SIMDVector rawNormal = VectorZero();
    SIMDVector rawTangent = VectorZero();
    for(u32 i = 0u; i < 3u; ++i){
        const u32 sourceVertex = generated.sourceVertices[i];
        const f32 weight = generated.bary[i];
        if(sourceVertex >= instance.restVertices.size() || !IsFinite(weight) || weight < 0.0f)
            return false;

        const DeformableVertexRest& source = instance.restVertices[sourceVertex];
        const SIMDVector weightVector = VectorReplicate(weight);
        rawNormal = VectorMultiplyAdd(LoadRestVertexNormal(source), weightVector, rawNormal);
        rawTangent = VectorMultiplyAdd(VectorSetW(LoadRestVertexTangent(source), 0.0f), weightVector, rawTangent);
    }

    const SIMDVector normal = Core::Geometry::FrameNormalizeDirection(rawNormal, fallbackNormal);
    SIMDVector tangent;
    SIMDVector bitangent;
    ResolveTangentBitangentVectors(normal, rawTangent, fallbackTangent, tangent, bitangent);
    if(!FiniteVec3(normal) || !FiniteVec3(tangent))
        return false;

    StoreFloat(normal, &vertex.normal);
    StoreFloat(VectorSetW(tangent, 1.0f), &vertex.tangent);
    if(
        !BuildBlendedVertexUv0(
            instance.restVertices,
            generated.sourceVertices,
            generated.bary,
            vertex.uv0
        )
    )
        return false;
    if(
        !BuildBlendedVertexColor(
            instance.restVertices,
            generated.sourceVertices,
            generated.bary,
            vertex.color0
        )
    )
        return false;
    if(!DeformableValidation::ValidRestVertexFrame(vertex))
        return false;

    if(!newSkin.empty()){
        SkinInfluence4 skin;
        if(
            !BuildBlendedSkinInfluence(
                instance.skin,
                generated.sourceVertices,
                generated.bary,
                skin
            )
        )
            return false;
        newSkin.push_back(skin);
    }

    if(!newSourceSamples.empty()){
        SourceSample sample;
        if(
            !BuildBlendedSourceSample(
                instance.sourceSamples,
                generated.sourceVertices,
                generated.bary,
                fallbackSourceSample,
                instance.sourceTriangleCount,
                sample
            )
        )
            return false;
        newSourceSamples.push_back(sample);
    }

    newRestVertices.push_back(vertex);
    return true;
}

[[nodiscard]] bool TransferSurfaceRemeshMorphDeltas(
    Vector<DeformableMorph>& morphs,
    const Vector<SurfaceRemeshGeneratedVertex, Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>>& generatedVertices
){
    if(morphs.empty() || generatedVertices.empty())
        return true;

    Core::Alloc::ScratchArena<> scratchArena;
    MorphDeltaLookup lookup(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        Core::Alloc::ScratchAllocator<Pair<const u32, usize>>(scratchArena)
    );
    for(DeformableMorph& morph : morphs){
        const usize sourceDeltaCount = morph.deltas.size();
        if(sourceDeltaCount == 0u)
            continue;
        if(
            generatedVertices.size() > static_cast<usize>(Limit<u32>::s_Max)
            || morph.deltas.size() > static_cast<usize>(Limit<u32>::s_Max) - generatedVertices.size()
        )
            return false;
        if(!BuildMorphDeltaLookup(morph, sourceDeltaCount, lookup))
            return false;

        morph.deltas.reserve(morph.deltas.size() + generatedVertices.size());
        for(const SurfaceRemeshGeneratedVertex& generated : generatedVertices){
            if(
                !AppendBlendedMorphDelta(
                    morph.deltas,
                    morph,
                    lookup,
                    generated.sourceVertices,
                    generated.bary,
                    generated.vertex
                )
            )
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool CommitRemeshedHoleImpl(
    DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    const bool requireUploadedRuntimePayload,
    const u32 wallLoopCutCount,
    DeformableHoleEditResult* outResult
){
    if(outResult)
        *outResult = DeformableHoleEditResult{};

    const bool validPayload = requireUploadedRuntimePayload
        ? ValidateUploadedRuntimePayload(instance)
        : ValidateRuntimePayload(instance)
    ;
    const bool validParams = ValidateParams(instance, params);
    const bool validWallLoopCutCount = ValidWallLoopCutCount(wallLoopCutCount);
    const bool validWallLoopDepth = wallLoopCutCount == 0u || params.depth > DeformableRuntime::s_Epsilon;
    if(!validPayload || !validParams || !validWallLoopCutCount || !validWallLoopDepth || !UseOperatorSurfaceRemesh(params)){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole validation failed (entity={} runtime_mesh={} uploaded_required={} valid_payload={} valid_params={} valid_loop_cuts={} valid_loop_depth={} dirty_flags={} revision={} triangle={})")
            , instance.entity.id
            , instance.handle.value
            , requireUploadedRuntimePayload ? 1u : 0u
            , validPayload ? 1u : 0u
            , validParams ? 1u : 0u
            , validWallLoopCutCount ? 1u : 0u
            , validWallLoopDepth ? 1u : 0u
            , static_cast<u32>(instance.dirtyFlags)
            , instance.editRevision
            , params.posedHit.triangle
        );
        return false;
    }

    u32 hitTriangleIndices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, params.posedHit.triangle, hitTriangleIndices))
        return false;

    f32 hitBary[3] = {};
    if(!DeformableValidation::NormalizeSourceBarycentric(params.posedHit.bary.values, hitBary))
        return false;

    SourceSample wallSourceSample{};
    if(!ResolveDeformableRestSurfaceSample(instance, params.posedHit.triangle, hitBary, wallSourceSample))
        return false;

    if(instance.sourceSamples.empty() || instance.sourceSamples.size() != instance.restVertices.size() || instance.sourceTriangleCount == 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole source samples are invalid (entity={} runtime_mesh={} vertices={} source_samples={} source_triangles={})")
            , instance.entity.id
            , instance.handle.value
            , instance.restVertices.size()
            , instance.sourceSamples.size()
            , instance.sourceTriangleCount
        );
        return false;
    }

    Core::Alloc::ScratchArena<> scratchArena;
    HoleFrame frame;
    Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>> orderedBoundaryEdges{
        Core::Alloc::ScratchAllocator<EdgeRecord>(scratchArena)
    };
    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>> restPositions{
        Core::Alloc::ScratchAllocator<Float3U>(scratchArena)
    };
    Vector<SurfaceRemeshTriangle, Core::Alloc::ScratchAllocator<SurfaceRemeshTriangle>> surfaceTriangles{
        Core::Alloc::ScratchAllocator<SurfaceRemeshTriangle>(scratchArena)
    };
    Vector<SurfaceRemeshGeneratedVertex, Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>> generatedVertices{
        Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>(scratchArena)
    };
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> affectedTriangleIndices{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
    u32 affectedTriangleCount = 0u;
    if(
        !BuildOperatorSurfaceRemeshPlan(
            instance,
            params,
            frame,
            orderedBoundaryEdges,
            restPositions,
            surfaceTriangles,
            generatedVertices,
            affectedTriangleIndices,
            affectedTriangleCount,
            scratchArena
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole plan failed (entity={} runtime_mesh={} hit_triangle={} radius={} ellipse={} depth={} footprint_vertices={})")
            , instance.entity.id
            , instance.handle.value
            , params.posedHit.triangle
            , params.radius
            , params.ellipseRatio
            , params.depth
            , params.operatorFootprint.vertexCount
        );
        return false;
    }

    const usize triangleCount = instance.indices.size() / 3u;
    const bool hasEditMaskPerTriangle = !instance.editMaskPerTriangle.empty();
    auto resolveValidatedEditMask = [&instance, hasEditMaskPerTriangle](const usize triangle){
        return
            hasEditMaskPerTriangle
                ? instance.editMaskPerTriangle[triangle]
                : s_DeformableEditMaskDefault
        ;
    };

    DeformableEditMaskFlags removedEditMaskFlags = 0u;
    for(const u32 affectedTriangleIndex : affectedTriangleIndices){
        if(affectedTriangleIndex >= triangleCount)
            return false;

        const DeformableEditMaskFlags editMaskFlags = resolveValidatedEditMask(affectedTriangleIndex);
        if(!DeformableEditMaskAllowsCommit(editMaskFlags))
            return false;

        removedEditMaskFlags = static_cast<DeformableEditMaskFlags>(removedEditMaskFlags | editMaskFlags);
    }
    if(removedEditMaskFlags == 0u)
        removedEditMaskFlags = s_DeformableEditMaskDefault;

    const bool addWall = params.depth > DeformableRuntime::s_Epsilon;
    const usize wallBandCount = addWall ? static_cast<usize>(wallLoopCutCount) + 1u : 0u;
    usize wallVertexCount = 0u;
    usize totalWallVertexCount = 0u;
    usize capTriangleCount = 0u;
    if(addWall){
        if(
            wallBandCount == 0u
            || wallBandCount > Limit<usize>::s_Max / 6u
            || orderedBoundaryEdges.size() > Limit<usize>::s_Max / (6u * wallBandCount)
        )
            return false;

        wallVertexCount = orderedBoundaryEdges.size();
        if(wallVertexCount < 3u || wallVertexCount > Limit<usize>::s_Max / wallBandCount)
            return false;
        totalWallVertexCount = wallVertexCount * wallBandCount;
        capTriangleCount = wallVertexCount - 2u;
    }

    const usize surfaceIndexCount = surfaceTriangles.size() * 3u;
    const usize wallIndexCount = addWall ? wallVertexCount * 6u * wallBandCount : 0u;
    const usize capIndexCount = capTriangleCount * 3u;
    if(
        surfaceTriangles.size() > static_cast<usize>(Limit<u32>::s_Max / 3u)
        || wallIndexCount > Limit<usize>::s_Max - capIndexCount
        || wallIndexCount + capIndexCount > Limit<usize>::s_Max - surfaceIndexCount
        || surfaceIndexCount + wallIndexCount + capIndexCount > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    usize reservedVertexCount = instance.restVertices.size();
    if(generatedVertices.size() > Limit<usize>::s_Max - reservedVertexCount)
        return false;
    reservedVertexCount += generatedVertices.size();
    if(totalWallVertexCount > Limit<usize>::s_Max - reservedVertexCount)
        return false;
    reservedVertexCount += totalWallVertexCount;
    if(reservedVertexCount > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    Vector<DeformableVertexRest> newRestVertices;
    Vector<SkinInfluence4> newSkin;
    Vector<SourceSample> newSourceSamples;
    Vector<DeformableEditMaskFlags> newEditMaskPerTriangle;
    Vector<DeformableMorph> newMorphs;
    newRestVertices.reserve(reservedVertexCount);
    AssignTriviallyCopyableVector(newRestVertices, instance.restVertices);
    if(!instance.skin.empty()){
        newSkin.reserve(reservedVertexCount);
        AssignTriviallyCopyableVector(newSkin, instance.skin);
    }
    if(!instance.sourceSamples.empty()){
        newSourceSamples.reserve(reservedVertexCount);
        AssignTriviallyCopyableVector(newSourceSamples, instance.sourceSamples);
    }
    newMorphs = instance.morphs;
    if(hasEditMaskPerTriangle)
        newEditMaskPerTriangle.reserve((surfaceIndexCount + wallIndexCount + capIndexCount) / 3u);

    for(const SurfaceRemeshGeneratedVertex& generated : generatedVertices){
        if(
            !AppendSurfaceRemeshGeneratedRestVertex(
                instance,
                generated,
                wallSourceSample,
                frame.normal,
                frame.tangent,
                newRestVertices,
                newSkin,
                newSourceSamples
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole failed while appending generated surface vertex (entity={} runtime_mesh={} generated_vertex={} source_triangle={})")
                , instance.entity.id
                , instance.handle.value
                , generated.vertex
                , generated.sourceTriangle
            );
            return false;
        }
    }
    if(!TransferSurfaceRemeshMorphDeltas(newMorphs, generatedVertices)){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole failed while transferring generated surface morph deltas (entity={} runtime_mesh={} generated_vertices={} morphs={})")
            , instance.entity.id
            , instance.handle.value
            , generatedVertices.size()
            , newMorphs.size()
        );
        return false;
    }

    Vector<u32> newIndices;
    newIndices.reserve(surfaceIndexCount + wallIndexCount + capIndexCount);
    for(usize surfaceTriangleIndex = 0u; surfaceTriangleIndex < surfaceTriangles.size(); ++surfaceTriangleIndex){
        const SurfaceRemeshTriangle& triangle = surfaceTriangles[surfaceTriangleIndex];
        if(
            triangle.sourceTriangle >= triangleCount
            || triangle.indices[0u] >= newRestVertices.size()
            || triangle.indices[1u] >= newRestVertices.size()
            || triangle.indices[2u] >= newRestVertices.size()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole produced an invalid surface triangle (entity={} runtime_mesh={} source_triangle={} vertices=({},{},{}))")
                , instance.entity.id
                , instance.handle.value
                , triangle.sourceTriangle
                , triangle.indices[0u]
                , triangle.indices[1u]
                , triangle.indices[2u]
            );
            return false;
        }

        newIndices.push_back(triangle.indices[0u]);
        newIndices.push_back(triangle.indices[1u]);
        newIndices.push_back(triangle.indices[2u]);
        if(hasEditMaskPerTriangle)
            newEditMaskPerTriangle.push_back(resolveValidatedEditMask(triangle.sourceTriangle));
    }

    u32 addedTriangleCount = 0u;
    u32 addedVertexCount = 0u;
    u32 firstWallVertex = Limit<u32>::s_Max;
    u32 addedWallVertexCount = 0u;
    if(addWall){
        const usize boundaryVertexCount = orderedBoundaryEdges.size();
        firstWallVertex = static_cast<u32>(
            newRestVertices.size() + ((wallBandCount - 1u) * boundaryVertexCount)
        );
        addedWallVertexCount = static_cast<u32>(boundaryVertexCount);

        Core::Geometry::MeshTopologyBoundaryLoopFrame topologyFrame;
        StoreFloat(frame.center, &topologyFrame.center);
        StoreFloat(frame.tangent, &topologyFrame.tangent);
        StoreFloat(frame.bitangent, &topologyFrame.bitangent);

        Vector<Core::Geometry::SurfacePatchWallVertex, Core::Alloc::ScratchAllocator<Core::Geometry::SurfacePatchWallVertex>> wallVertexPlan{
            Core::Alloc::ScratchAllocator<Core::Geometry::SurfacePatchWallVertex>(scratchArena)
        };
        wallVertexPlan.resize(totalWallVertexCount);
        if(
            !Core::Geometry::BuildSurfacePatchWallVertices(
                orderedBoundaryEdges,
                restPositions,
                topologyFrame,
                frame.normal,
                params.depth,
                wallBandCount,
                wallVertexPlan.data(),
                wallVertexPlan.size()
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole failed while building wall vertices (entity={} runtime_mesh={} boundary_vertices={} wall_bands={})")
                , instance.entity.id
                , instance.handle.value
                , orderedBoundaryEdges.size()
                , wallBandCount
            );
            return false;
        }
        if(
            !ApplyOperatorProfileToWallVertexPlan(
                orderedBoundaryEdges,
                restPositions,
                frame,
                params,
                wallVertexPlan.data(),
                wallVertexPlan.size()
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole failed while applying operator profile to wall vertices (entity={} runtime_mesh={} boundary_vertices={} profile_samples={})")
                , instance.entity.id
                , instance.handle.value
                , orderedBoundaryEdges.size()
                , params.operatorProfile.sampleCount
            );
            return false;
        }
        if(
            !ApplySourceContinuationUvsToWallVertexPlan(
                newRestVertices,
                wallVertexPlan.data(),
                wallVertexPlan.size()
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole failed while assigning wall continuation UVs (entity={} runtime_mesh={} wall_vertices={})")
                , instance.entity.id
                , instance.handle.value
                , wallVertexPlan.size()
            );
            return false;
        }

        Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>> bandOuterEdges = orderedBoundaryEdges;
        Vector<u32, Core::Alloc::ScratchAllocator<u32>> ringVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        ringVertices.resize(boundaryVertexCount);

        auto appendPlannedVertex = [&](
            const Core::Geometry::SurfacePatchWallVertex& plannedVertex,
            const SIMDVector normal,
            const SIMDVector tangent,
            const Float2U& uv0,
            u32& outVertex){
            Float4U innerColor;
            if(
                !BuildBlendedVertexColor(
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
                if(
                    !BuildBlendedSkinInfluence(
                        newSkin,
                        plannedVertex.attributeVertices,
                        s_WallInnerInpaintWeights,
                        innerSkin
                    )
                )
                    return false;

                innerSkinPtr = &innerSkin;
            }

            if(
                !AppendWallVertex(
                    newRestVertices,
                    newSkin,
                    newSourceSamples,
                    plannedVertex.sourceVertex,
                    innerSkinPtr,
                    wallSourceSample,
                    innerColor,
                    LoadFloat(plannedVertex.position),
                    normal,
                    tangent,
                    uv0.x,
                    uv0.y,
                    outVertex
                )
            )
                return false;

            addedVertexCount += 1u;
            return true;
        };

        for(usize ringIndex = 0u; ringIndex < wallBandCount; ++ringIndex){
            const usize wallPlanBase = ringIndex * boundaryVertexCount;
            for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
                const Core::Geometry::SurfacePatchWallVertex& plannedVertex = wallVertexPlan[wallPlanBase + edgeIndex];
                u32 wallVertex = 0u;
                if(
                    !appendPlannedVertex(
                        plannedVertex,
                        LoadFloat(plannedVertex.normal),
                        LoadFloat(plannedVertex.tangent),
                        plannedVertex.uv0,
                        wallVertex
                    )
                ){
                    NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole failed while appending wall vertex (entity={} runtime_mesh={} ring={} edge={} source_vertex={})")
                        , instance.entity.id
                        , instance.handle.value
                        , ringIndex
                        , edgeIndex
                        , plannedVertex.sourceVertex
                    );
                    return false;
                }
                ringVertices[edgeIndex] = wallVertex;
            }

            if(!TransferWallMorphDeltas(newMorphs, orderedBoundaryEdges, ringVertices)){
                NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole failed while transferring wall morph deltas (entity={} runtime_mesh={} ring={} morphs={})")
                    , instance.entity.id
                    , instance.handle.value
                    , ringIndex
                    , newMorphs.size()
                );
                return false;
            }

            u32 wallAddedTriangleCount = 0u;
            if(!Core::Geometry::AppendWallTrianglePairs(bandOuterEdges, ringVertices, newIndices, &wallAddedTriangleCount)){
                NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole failed while appending wall triangles (entity={} runtime_mesh={} ring={} boundary_vertices={})")
                    , instance.entity.id
                    , instance.handle.value
                    , ringIndex
                    , boundaryVertexCount
                );
                return false;
            }
            if(hasEditMaskPerTriangle){
                for(u32 triangleOffset = 0u; triangleOffset < wallAddedTriangleCount; ++triangleOffset)
                    newEditMaskPerTriangle.push_back(removedEditMaskFlags);
            }
            addedTriangleCount += wallAddedTriangleCount;

            if(ringIndex + 1u < wallBandCount){
                if(
                    !Core::Geometry::BuildSurfacePatchRingEdges(
                        ringVertices.data(),
                        boundaryVertexCount,
                        bandOuterEdges
                    )
                ){
                    NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole failed while building wall ring edges (entity={} runtime_mesh={} ring={} boundary_vertices={})")
                        , instance.entity.id
                        , instance.handle.value
                        , ringIndex
                        , boundaryVertexCount
                    );
                    return false;
                }
            }
        }

        u32 capAddedTriangleCount = 0u;
        if(
            !AppendBottomCapTriangles(
                ringVertices,
                newRestVertices,
                frame.tangent,
                frame.bitangent,
                newIndices,
                &capAddedTriangleCount
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole failed while triangulating bottom cap (entity={} runtime_mesh={} cap_vertices={})")
                , instance.entity.id
                , instance.handle.value
                , ringVertices.size()
            );
            return false;
        }
        if(hasEditMaskPerTriangle){
            for(u32 triangleOffset = 0u; triangleOffset < capAddedTriangleCount; ++triangleOffset)
                newEditMaskPerTriangle.push_back(removedEditMaskFlags);
        }
        addedTriangleCount += capAddedTriangleCount;
        if(!BlendDeepestWallRingCapFrames(newRestVertices, firstWallVertex, addedWallVertexCount, frame.normal, frame.tangent)){
            NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole failed while blending cap frames (entity={} runtime_mesh={} first_wall_vertex={} wall_vertices={})")
                , instance.entity.id
                , instance.handle.value
                , firstWallVertex
                , addedWallVertexCount
            );
            return false;
        }
    }

    for(usize indexBase = 0u; indexBase < newIndices.size(); indexBase += 3u){
        const u32 i0 = newIndices[indexBase + 0u];
        const u32 i1 = newIndices[indexBase + 1u];
        const u32 i2 = newIndices[indexBase + 2u];
        if(
            i0 >= newRestVertices.size()
            || i1 >= newRestVertices.size()
            || i2 >= newRestVertices.size()
            || !DeformableValidation::ValidTriangle(newRestVertices, i0, i1, i2)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole produced degenerate triangle before tangent rebuild (entity={} runtime_mesh={} triangle={} vertices=({},{},{}))")
                , instance.entity.id
                , instance.handle.value
                , indexBase / 3u
                , i0
                , i1
                , i2
            );
            return false;
        }
    }

    // Continuation wall UVs can be degenerate by design; keep the authored remesh frames when the global rebuild falls back.
    DeformableValidation::ApplyCleanRestVertexTangentFrameRebuildIfPossible(newRestVertices, newIndices);
    RestoreStableOriginalSurfaceAttributes(instance.restVertices, newRestVertices);

    if(
        !DeformableValidation::ValidRuntimePayloadArrays(
            newRestVertices,
            newIndices,
            instance.sourceTriangleCount,
            instance.skeletonJointCount,
            newSkin,
            instance.inverseBindMatrices,
            newSourceSamples,
            hasEditMaskPerTriangle ? newEditMaskPerTriangle : instance.editMaskPerTriangle,
            newMorphs
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: remeshed hole produced invalid runtime payload arrays (entity={} runtime_mesh={} vertices={} triangles={} skin={} source_samples={} edit_masks={} morphs={})")
            , instance.entity.id
            , instance.handle.value
            , newRestVertices.size()
            , newIndices.size() / 3u
            , newSkin.size()
            , newSourceSamples.size()
            , newEditMaskPerTriangle.size()
            , newMorphs.size()
        );
        return false;
    }

    instance.restVertices = Move(newRestVertices);
    instance.skin = Move(newSkin);
    instance.sourceSamples = Move(newSourceSamples);
    instance.morphs = Move(newMorphs);
    if(hasEditMaskPerTriangle)
        instance.editMaskPerTriangle = Move(newEditMaskPerTriangle);
    instance.indices = Move(newIndices);
    ++instance.editRevision;
    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(instance.dirtyFlags | RuntimeMeshDirtyFlag::All);

    if(outResult){
        outResult->removedTriangleCount = affectedTriangleCount;
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
    const ReplayResultValidation::Enum validation
){
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
    DeformableHoleEditResult& outReplayResult
){
    outReplayResult = DeformableHoleEditResult{};

    const usize triangleCount = replayInstance.indices.size() / 3u;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        DeformableHoleEditParams params;
        if(!BuildReplayHoleParams(replayInstance, replayRecord, triangle, params))
            continue;

        DeformableRuntimeMeshInstance candidateInstance = replayInstance;
        DeformableHoleEditResult candidateResult;
        if(
            !CommitRemeshedHoleImpl(
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
    const DeformableHoleEditResult& replayResult
){
    ++replay.appliedEditCount;
    replay.topologyChanged = replay.topologyChanged || HoleResultChangesTopology(replayResult);
}

[[nodiscard]] bool RebuildReplayAccessories(
    const DeformableSurfaceEditState& sourceState,
    const DeformableSurfaceEditId removedAnchorEditId,
    DeformableSurfaceEditState& replayState,
    u32* outRemovedAccessoryCount
){
    if(sourceState.accessories.empty())
        return true;

    replayState.accessories.reserve(sourceState.accessories.size());
    Core::Alloc::ScratchArena<> scratchArena;
    SurfaceEditRecordLookup replayEditRecords(
        0,
        Hasher<DeformableSurfaceEditId>(),
        EqualTo<DeformableSurfaceEditId>(),
        Core::Alloc::ScratchAllocator<SurfaceEditRecordLookupPair>(scratchArena)
    );
    if(!BuildSurfaceEditRecordLookup(replayState, replayEditRecords))
        return false;

    for(const DeformableAccessoryAttachmentRecord& accessory : sourceState.accessories){
        if(outRemovedAccessoryCount && accessory.anchorEditId == removedAnchorEditId){
            ++(*outRemovedAccessoryCount);
            continue;
        }

        const DeformableSurfaceEditRecord* anchorEdit = FindEditRecordById(replayEditRecords, accessory.anchorEditId);
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
    DeformableSurfaceEditReplayResult& result
){
    result = DeformableSurfaceEditReplayResult{};
    for(const DeformableSurfaceEditRecord& record : state.edits){
        DeformableSurfaceEditRecord replayRecord = record;
        DeformableHoleEditResult replayResult;
        if(
            !ReplaySurfaceEditRecord(
                replayInstance,
                record,
                replayRecord,
                ReplayResultValidation::Exact,
                replayResult
            )
        )
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
    DeformableSurfaceEditRedoEntry* outRedoEntry
){
    outUndoState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditUndoResult{};
    if(outRedoEntry)
        *outRedoEntry = DeformableSurfaceEditRedoEntry{};
    if(!ValidSurfaceEditState(state) || state.edits.empty())
        return false;

    const DeformableSurfaceEditId undoneEditId = state.edits.back().editId;
    if(outRedoEntry){
        outRedoEntry->edit = state.edits.back();
        outRedoEntry->accessories.reserve(state.accessories.size());
    }

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
    return
        state.edits.empty()
            ? 0u
            : state.edits.back().result.editRevision
    ;
}

[[nodiscard]] bool BuildRedoSurfaceEditState(
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditRedoEntry& redoEntry,
    DeformableSurfaceEditState& outRedoState,
    DeformableSurfaceEditRedoResult& outResult
){
    outRedoState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditRedoResult{};
    if(
        !ValidSurfaceEditState(state)
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
    AssignTriviallyCopyableVector(outRedoState.edits, state.edits);
    outRedoState.edits.push_back(redoEntry.edit);

    outRedoState.accessories.reserve(state.accessories.size() + redoEntry.accessories.size());
    AppendTriviallyCopyableVector(outRedoState.accessories, state.accessories);
    AppendTriviallyCopyableVector(outRedoState.accessories, redoEntry.accessories);

    outResult.redoneEditId = redoEntry.edit.editId;
    outResult.restoredAccessoryCount = static_cast<u32>(redoEntry.accessories.size());
    return ValidSurfaceEditState(outRedoState);
}

[[nodiscard]] bool ReplaySurfaceEditRecordsWithoutEdit(
    DeformableRuntimeMeshInstance& replayInstance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    DeformableSurfaceEditState& outHealedState,
    DeformableSurfaceEditHealResult& outResult
){
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
        if(
            !ReplaySurfaceEditRecord(
                replayInstance,
                record,
                replayRecord,
                ReplayResultValidation::Shape,
                replayResult
            )
        )
            return false;

        outHealedState.edits.push_back(replayRecord);
        AccumulateSurfaceEditReplayResult(outResult.replay, replayResult);
    }

    if(!RebuildReplayAccessories(state, editId, outHealedState, &outResult.removedAccessoryCount))
        return false;

    outResult.replay.finalEditRevision = replayInstance.editRevision;
    return ValidSurfaceEditState(outHealedState);
}

template<typename MutateRecordFunc>
[[nodiscard]] bool ReplaySurfaceEditRecordsWithMutatedHole(
    DeformableRuntimeMeshInstance& replayInstance,
    const DeformableSurfaceEditState& state,
    DeformableSurfaceEditState& outMutatedState,
    DeformableSurfaceEditReplayResult& outReplayResult,
    MutateRecordFunc&& mutateRecord
){
    bool mutatedEditFound = false;
    bool replayDependsOnMutatedEdit = false;
    outMutatedState.edits.reserve(state.edits.size());
    for(const DeformableSurfaceEditRecord& record : state.edits){
        if(replayInstance.editRevision == Limit<u32>::s_Max)
            return false;

        DeformableSurfaceEditRecord replayRecord = record;
        replayRecord.hole.baseEditRevision = replayInstance.editRevision;
        replayRecord.result.editRevision = replayInstance.editRevision + 1u;

        bool mutateThisRecord = false;
        mutateRecord(record, replayRecord, mutateThisRecord);
        if(mutateThisRecord){
            mutatedEditFound = true;
            replayDependsOnMutatedEdit = true;
        }

        DeformableHoleEditResult replayResult;
        const ReplayResultValidation::Enum validation = mutateThisRecord
            ? ReplayResultValidation::None
            : replayDependsOnMutatedEdit ? ReplayResultValidation::Shape : ReplayResultValidation::Exact
        ;
        if(!ReplaySurfaceEditRecord(replayInstance, record, replayRecord, validation, replayResult))
            return false;

        outMutatedState.edits.push_back(replayRecord);
        AccumulateSurfaceEditReplayResult(outReplayResult, replayResult);
    }
    if(!mutatedEditFound)
        return false;

    if(!RebuildReplayAccessories(state, 0u, outMutatedState, nullptr))
        return false;

    outReplayResult.finalEditRevision = replayInstance.editRevision;
    return ValidSurfaceEditState(outMutatedState);
}

[[nodiscard]] bool ReplaySurfaceEditRecordsWithResizedHole(
    DeformableRuntimeMeshInstance& replayInstance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    const f32 radius,
    const f32 ellipseRatio,
    const f32 depth,
    DeformableSurfaceEditState& outResizedState,
    DeformableSurfaceEditResizeResult& outResult
){
    outResizedState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditResizeResult{};
    if(
        !ValidSurfaceEditState(state)
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

    return ReplaySurfaceEditRecordsWithMutatedHole(
        replayInstance,
        state,
        outResizedState,
        outResult.replay,
        [&](const DeformableSurfaceEditRecord& record, DeformableSurfaceEditRecord& replayRecord, bool& outMutateThisRecord){
            outMutateThisRecord = record.editId == editId;
            if(!outMutateThisRecord)
                return;

            replayRecord.hole.radius = radius;
            replayRecord.hole.ellipseRatio = ellipseRatio;
            replayRecord.hole.depth = depth;
        }
    );
}

[[nodiscard]] bool ReplaySurfaceEditRecordsWithMovedHole(
    DeformableRuntimeMeshInstance& replayInstance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    const DeformableSurfaceHoleEditRecord& moveTarget,
    DeformableSurfaceEditState& outMovedState,
    DeformableSurfaceEditMoveResult& outResult
){
    outMovedState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditMoveResult{};
    if(
        !ValidSurfaceEditState(state)
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

    return ReplaySurfaceEditRecordsWithMutatedHole(
        replayInstance,
        state,
        outMovedState,
        outResult.replay,
        [&](const DeformableSurfaceEditRecord& record, DeformableSurfaceEditRecord& replayRecord, bool& outMutateThisRecord){
            outMutateThisRecord = record.editId == editId;
            if(!outMutateThisRecord)
                return;

            replayRecord.hole.restSample = moveTarget.restSample;
            replayRecord.hole.restPosition = moveTarget.restPosition;
            replayRecord.hole.restNormal = moveTarget.restNormal;
        }
    );
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
    DeformableSurfaceEditPatchResult& outResult
){
    outPatchedState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditPatchResult{};
    if(
        !ValidSurfaceEditState(state)
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

    return ReplaySurfaceEditRecordsWithMutatedHole(
        replayInstance,
        state,
        outPatchedState,
        outResult.replay,
        [&](const DeformableSurfaceEditRecord& record, DeformableSurfaceEditRecord& replayRecord, bool& outMutateThisRecord){
            outMutateThisRecord = record.editId == editId;
            if(!outMutateThisRecord)
                return;

            replayRecord.hole.restSample = patchTarget.restSample;
            replayRecord.hole.restPosition = patchTarget.restPosition;
            replayRecord.hole.restNormal = patchTarget.restNormal;
            replayRecord.hole.radius = radius;
            replayRecord.hole.ellipseRatio = ellipseRatio;
            replayRecord.hole.depth = depth;
        }
    );
}

[[nodiscard]] bool ReplaySurfaceEditRecordsWithAddedLoopCut(
    DeformableRuntimeMeshInstance& replayInstance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditId editId,
    DeformableSurfaceEditState& outLoopCutState,
    DeformableSurfaceEditLoopCutResult& outResult
){
    outLoopCutState = DeformableSurfaceEditState{};
    outResult = DeformableSurfaceEditLoopCutResult{};
    if(!ValidSurfaceEditState(state) || !ValidSurfaceEditId(editId))
        return false;

    const DeformableSurfaceEditRecord* loopCutEdit = FindEditRecordById(state, editId);
    if(
        !loopCutEdit
        || loopCutEdit->type != DeformableSurfaceEditRecordType::Hole
        || loopCutEdit->hole.depth <= DeformableRuntime::s_Epsilon
        || loopCutEdit->hole.wallLoopCutCount >= s_MaxWallLoopCutCount
    )
        return false;

    outResult.loopCutEditId = editId;
    outResult.oldLoopCutCount = loopCutEdit->hole.wallLoopCutCount;
    outResult.newLoopCutCount = loopCutEdit->hole.wallLoopCutCount + 1u;

    return ReplaySurfaceEditRecordsWithMutatedHole(
        replayInstance,
        state,
        outLoopCutState,
        outResult.replay,
        [&](const DeformableSurfaceEditRecord& record, DeformableSurfaceEditRecord& replayRecord, bool& outMutateThisRecord){
            outMutateThisRecord = record.editId == editId;
            if(!outMutateThisRecord)
                return;

            ++replayRecord.hole.wallLoopCutCount;
        }
    );
}

[[nodiscard]] bool PrepareCleanReplayInstance(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableRuntimeMeshInstance& cleanBaseInstance,
    DeformableRuntimeMeshInstance& outReplayInstance
){
    outReplayInstance = DeformableRuntimeMeshInstance{};
    if(
        cleanBaseInstance.editRevision != 0u
        || cleanBaseInstance.source.name() != instance.source.name()
        || cleanBaseInstance.geometryClass != instance.geometryClass
        || !GeometryClassAllowsRuntimeDeform(cleanBaseInstance.geometryClass)
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


bool ApplySurfaceEditState(
    DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditReplayContext& context,
    DeformableSurfaceEditReplayResult* outResult
){
    if(outResult)
        *outResult = DeformableSurfaceEditReplayResult{};

    if(
        !__hidden_deformable_surface_edit::ValidSurfaceEditState(state)
        || !__hidden_deformable_surface_edit::ValidateRuntimePayload(instance)
        || !GeometryClassAllowsRuntimeDeform(instance.geometryClass)
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
    DeformableSurfaceEditHistory* history
){
    if(outResult)
        *outResult = DeformableSurfaceEditUndoResult{};

    DeformableSurfaceEditState undoState;
    DeformableSurfaceEditUndoResult result;
    DeformableSurfaceEditRedoEntry redoEntry;
    if(
        !__hidden_deformable_surface_edit::BuildUndoSurfaceEditState(
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
    DeformableSurfaceEditRedoResult* outResult
){
    if(outResult)
        *outResult = DeformableSurfaceEditRedoResult{};
    if(history.redoStack.empty())
        return false;

    DeformableSurfaceEditState redoState;
    DeformableSurfaceEditRedoResult result;
    if(
        !__hidden_deformable_surface_edit::BuildRedoSurfaceEditState(
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
    DeformableSurfaceEditHealResult* outResult
){
    if(outResult)
        *outResult = DeformableSurfaceEditHealResult{};

    DeformableRuntimeMeshInstance replayInstance;
    if(!__hidden_deformable_surface_edit::PrepareCleanReplayInstance(instance, cleanBaseInstance, replayInstance))
        return false;

    DeformableSurfaceEditState healedState;
    DeformableSurfaceEditHealResult result;
    if(
        !__hidden_deformable_surface_edit::ReplaySurfaceEditRecordsWithoutEdit(
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
    DeformableSurfaceEditResizeResult* outResult
){
    if(outResult)
        *outResult = DeformableSurfaceEditResizeResult{};

    DeformableRuntimeMeshInstance replayInstance;
    if(!__hidden_deformable_surface_edit::PrepareCleanReplayInstance(instance, cleanBaseInstance, replayInstance))
        return false;

    DeformableSurfaceEditState resizedState;
    DeformableSurfaceEditResizeResult result;
    if(
        !__hidden_deformable_surface_edit::ReplaySurfaceEditRecordsWithResizedHole(
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
    DeformableSurfaceEditMoveResult* outResult
){
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
    if(
        !__hidden_deformable_surface_edit::ReplaySurfaceEditRecordsWithMovedHole(
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
    DeformableSurfaceEditPatchResult* outResult
){
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
    if(
        !__hidden_deformable_surface_edit::ReplaySurfaceEditRecordsWithPatchedHole(
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
    DeformableSurfaceEditLoopCutResult* outResult
){
    if(outResult)
        *outResult = DeformableSurfaceEditLoopCutResult{};

    DeformableRuntimeMeshInstance replayInstance;
    if(!__hidden_deformable_surface_edit::PrepareCleanReplayInstance(instance, cleanBaseInstance, replayInstance))
        return false;

    DeformableSurfaceEditState loopCutState;
    DeformableSurfaceEditLoopCutResult result;
    if(
        !__hidden_deformable_surface_edit::ReplaySurfaceEditRecordsWithAddedLoopCut(
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

