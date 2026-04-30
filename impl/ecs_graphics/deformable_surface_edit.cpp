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
static constexpr u32 s_SurfaceEditStateVersion = 8u;
static constexpr u32 s_MinWallLoopVertexCount = 3u;
static constexpr u32 s_MaxWallLoopCutCount = 8u;
static constexpr f32 s_MinHoleBoundaryKeptFaceNormalDot = 0.5f;
static constexpr f32 s_HolePreviewSurfaceOffsetMin = 0.001f;
static constexpr f32 s_HolePreviewSurfaceOffsetRadiusScale = 0.025f;
static constexpr f32 s_OperatorFootprintPlaneEpsilon = 0.0001f;
static constexpr f32 s_OperatorFootprintPointEpsilonSq = 0.0000000001f;
static constexpr f32 s_OperatorFootprintAreaEpsilon = 0.000001f;
static constexpr f32 s_OperatorProfileDepthEpsilon = 0.00001f;
static constexpr f32 s_OperatorProfileScaleEpsilon = 0.000001f;
static constexpr f32 s_MinOperatorProfileWallScale = 0.05f;
static constexpr f32 s_MaxOperatorProfileScale = 16.0f;
static constexpr f32 s_SurfaceRemeshClipEpsilon = 0.00001f;
static constexpr f32 s_SurfaceRemeshAreaEpsilon = 0.0000001f;
static constexpr f32 s_SurfaceRemeshVertexMergeDistanceSq = 0.0000000001f;
static constexpr f32 s_BottomCapTriangulationAreaEpsilon = 0.000001f;
static constexpr Float4U s_HolePreviewColor = Float4U(1.0f, 1.0f, 1.0f, 1.0f);

struct HoleFrame{
    SIMDVector center = VectorZero();
    SIMDVector normal = VectorZero();
    SIMDVector tangent = VectorZero();
    SIMDVector bitangent = VectorZero();
};

struct OperatorFootprintPoint{
    f32 x = 0.0f;
    f32 y = 0.0f;
};

struct SurfaceRemeshClipPoint{
    Float2U local = Float2U(0.0f, 0.0f);
    f32 bary[3] = {};
    u32 originalVertex = Limit<u32>::s_Max;
};

struct SurfaceRemeshGeneratedVertex{
    u32 vertex = Limit<u32>::s_Max;
    u32 sourceTriangle = Limit<u32>::s_Max;
    u32 sourceVertices[3] = {};
    f32 bary[3] = {};
    Float2U local = Float2U(0.0f, 0.0f);
    Float3U position = Float3U(0.0f, 0.0f, 0.0f);
};

struct SurfaceRemeshTriangle{
    u32 indices[3] = {};
    u32 sourceTriangle = Limit<u32>::s_Max;
};

[[nodiscard]] bool FiniteVec3(const SIMDVector value){
    return DeformableValidation::FiniteVector(value, 0x7u);
}

[[nodiscard]] bool FiniteOperatorPosition(const Float3U& position){
    return IsFinite(position.x) && IsFinite(position.y) && IsFinite(position.z);
}

[[nodiscard]] f32 OperatorFootprintCross(
    const OperatorFootprintPoint& a,
    const OperatorFootprintPoint& b,
    const OperatorFootprintPoint& c
){
    return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

[[nodiscard]] f32 OperatorFootprintDistanceSq(
    const OperatorFootprintPoint& a,
    const OperatorFootprintPoint& b
){
    const f32 dx = b.x - a.x;
    const f32 dy = b.y - a.y;
    return (dx * dx) + (dy * dy);
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

[[nodiscard]] SIMDVector TriangleCentroid(const DeformableRuntimeMeshInstance& instance, const u32 (&indices)[3]){
    SIMDVector centroid = VectorAdd(
        VectorAdd(LoadRestVertexPosition(instance.restVertices[indices[0]]), LoadRestVertexPosition(instance.restVertices[indices[1]])),
        LoadRestVertexPosition(instance.restVertices[indices[2]])
    );
    return VectorScale(centroid, 1.0f / 3.0f);
}

[[nodiscard]] bool TriangleContainsEdge(const u32 (&indices)[3], const u32 a, const u32 b){
    bool foundA = false;
    bool foundB = false;
    for(const u32 index : indices){
        foundA = foundA || index == a;
        foundB = foundB || index == b;
    }
    return foundA && foundB;
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
    if(VectorGetX(Vector3LengthSq(rawNormal)) <= Core::Geometry::s_FrameDirectionEpsilon)
        return false;

    outNormal = Core::Geometry::FrameNormalizeDirection(rawNormal, s_SIMDIdentityR2);
    return FiniteVec3(outNormal);
}

[[nodiscard]] bool BuildHoleFrame(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&triangleIndices)[3],
    const f32 (&bary)[3],
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
    ResolveTangentBitangentVectors(outFrame.normal, tangentVector, edge0, outFrame.tangent, outFrame.bitangent);
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

[[nodiscard]] bool ExactFloat3(const Float4& lhs, const Float4& rhs){
    return
        ExactF32(lhs.x, rhs.x)
        && ExactF32(lhs.y, rhs.y)
        && ExactF32(lhs.z, rhs.z)
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

[[nodiscard]] f32 OperatorFootprintSignedArea(const DeformableOperatorFootprint& footprint){
    f32 area = 0.0f;
    for(u32 i = 0u; i < footprint.vertexCount; ++i){
        const u32 next = (i + 1u) % footprint.vertexCount;
        area +=
            (footprint.vertices[i].x * footprint.vertices[next].y)
            - (footprint.vertices[i].y * footprint.vertices[next].x)
        ;
    }
    return area * 0.5f;
}

[[nodiscard]] bool ValidateOperatorFootprint(const DeformableOperatorFootprint& footprint){
    if(footprint.vertexCount == 0u)
        return true;
    if(
        footprint.vertexCount < 3u
        || footprint.vertexCount > s_DeformableOperatorFootprintMaxVertexCount
    )
        return false;

    for(u32 i = 0u; i < footprint.vertexCount; ++i){
        const Float2U& point = footprint.vertices[i];
        if(!IsFinite(point.x) || !IsFinite(point.y))
            return false;
    }

    const f32 area = OperatorFootprintSignedArea(footprint);
    return IsFinite(area) && Abs(area) > 0.000001f;
}

[[nodiscard]] bool ValidateOperatorProfile(const DeformableOperatorProfile& profile){
    if(profile.sampleCount == 0u)
        return true;
    if(
        profile.sampleCount < 2u
        || profile.sampleCount > s_DeformableOperatorProfileMaxSampleCount
    )
        return false;

    f32 previousDepth = -1.0f;
    for(u32 i = 0u; i < profile.sampleCount; ++i){
        const DeformableOperatorProfileSample& sample = profile.samples[i];
        if(
            !IsFinite(sample.depth)
            || !IsFinite(sample.scale)
            || !IsFinite(sample.center.x)
            || !IsFinite(sample.center.y)
            || sample.depth < -s_OperatorProfileDepthEpsilon
            || sample.depth > 1.0f + s_OperatorProfileDepthEpsilon
            || sample.scale < 0.0f
            || sample.scale > s_MaxOperatorProfileScale
        )
            return false;
        if(i == 0u){
            if(Abs(sample.depth) > s_OperatorProfileDepthEpsilon || sample.scale <= s_OperatorProfileScaleEpsilon)
                return false;
        }
        else if(sample.depth <= previousDepth + s_OperatorProfileDepthEpsilon)
            return false;
        previousDepth = sample.depth;
    }

    return Abs(profile.samples[profile.sampleCount - 1u].depth - 1.0f) <= s_OperatorProfileDepthEpsilon;
}

[[nodiscard]] bool AppendUniqueOperatorFootprintPoint(
    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>>& points,
    const f32 x,
    const f32 y
){
    if(!IsFinite(x) || !IsFinite(y))
        return false;

    const OperatorFootprintPoint point{ x, y };
    for(const OperatorFootprintPoint& existing : points){
        if(OperatorFootprintDistanceSq(existing, point) <= s_OperatorFootprintPointEpsilonSq)
            return true;
    }

    points.push_back(point);
    return true;
}

[[nodiscard]] bool AppendOperatorFootprintHullPoint(
    DeformableOperatorFootprint& footprint,
    const OperatorFootprintPoint& point
){
    if(footprint.vertexCount >= s_DeformableOperatorFootprintMaxVertexCount)
        return false;

    footprint.vertices[footprint.vertexCount] = Float2U(point.x, point.y);
    ++footprint.vertexCount;
    return true;
}

[[nodiscard]] bool BuildConvexOperatorFootprint(
    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>>& points,
    DeformableOperatorFootprint& outFootprint,
    Core::Alloc::ScratchArena<>& scratchArena
){
    outFootprint = DeformableOperatorFootprint{};
    if(points.size() < 3u)
        return false;

    Sort(points.begin(), points.end(), [](const OperatorFootprintPoint& lhs, const OperatorFootprintPoint& rhs){
        if(lhs.x != rhs.x)
            return lhs.x < rhs.x;
        return lhs.y < rhs.y;
    });

    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>> lower{
        Core::Alloc::ScratchAllocator<OperatorFootprintPoint>(scratchArena)
    };
    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>> upper{
        Core::Alloc::ScratchAllocator<OperatorFootprintPoint>(scratchArena)
    };
    lower.reserve(points.size());
    upper.reserve(points.size());

    for(const OperatorFootprintPoint& point : points){
        while(
            lower.size() >= 2u
            && OperatorFootprintCross(lower[lower.size() - 2u], lower[lower.size() - 1u], point)
                <= s_OperatorFootprintAreaEpsilon
        )
            lower.pop_back();
        lower.push_back(point);
    }

    for(usize i = points.size(); i > 0u; --i){
        const OperatorFootprintPoint& point = points[i - 1u];
        while(
            upper.size() >= 2u
            && OperatorFootprintCross(upper[upper.size() - 2u], upper[upper.size() - 1u], point)
                <= s_OperatorFootprintAreaEpsilon
        )
            upper.pop_back();
        upper.push_back(point);
    }

    if(lower.size() < 2u || upper.size() < 2u)
        return false;

    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>> hull{
        Core::Alloc::ScratchAllocator<OperatorFootprintPoint>(scratchArena)
    };
    hull.reserve(lower.size() + upper.size());
    for(const OperatorFootprintPoint& point : lower)
        hull.push_back(point);
    for(usize i = 1u; i + 1u < upper.size(); ++i)
        hull.push_back(upper[i]);

    if(hull.size() < 3u)
        return false;

    if(hull.size() <= s_DeformableOperatorFootprintMaxVertexCount){
        for(const OperatorFootprintPoint& point : hull){
            if(!AppendOperatorFootprintHullPoint(outFootprint, point))
                return false;
        }
    }
    else{
        for(u32 i = 0u; i < s_DeformableOperatorFootprintMaxVertexCount; ++i){
            const usize hullIndex = (static_cast<usize>(i) * hull.size()) / s_DeformableOperatorFootprintMaxVertexCount;
            if(hullIndex >= hull.size() || !AppendOperatorFootprintHullPoint(outFootprint, hull[hullIndex]))
                return false;
        }
    }

    return ValidateOperatorFootprint(outFootprint);
}

[[nodiscard]] bool BuildOperatorFootprintFromGeometryImpl(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorFootprint& outFootprint,
    Core::Alloc::ScratchArena<>& scratchArena
){
    outFootprint = DeformableOperatorFootprint{};
    if(vertices.empty())
        return false;

    f32 maxZ = 0.0f;
    bool foundPosition = false;
    for(const GeometryVertex& vertex : vertices){
        if(!FiniteOperatorPosition(vertex.position))
            return false;

        if(!foundPosition || vertex.position.z > maxZ){
            maxZ = vertex.position.z;
            foundPosition = true;
        }
    }
    if(!foundPosition)
        return false;

    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>> points{
        Core::Alloc::ScratchAllocator<OperatorFootprintPoint>(scratchArena)
    };
    points.reserve(vertices.size());

    const f32 planeEpsilon = Max(s_OperatorFootprintPlaneEpsilon, Abs(maxZ) * 0.00001f);
    for(const GeometryVertex& vertex : vertices){
        if(maxZ - vertex.position.z > planeEpsilon)
            continue;
        if(!AppendUniqueOperatorFootprintPoint(points, vertex.position.x, vertex.position.y))
            return false;
    }

    if(points.size() < 3u){
        points.clear();
        for(const GeometryVertex& vertex : vertices){
            if(!AppendUniqueOperatorFootprintPoint(points, vertex.position.x, vertex.position.y))
                return false;
        }
    }

    return BuildConvexOperatorFootprint(points, outFootprint, scratchArena);
}

[[nodiscard]] bool AppendUniqueOperatorProfileZ(
    Vector<f32, Core::Alloc::ScratchAllocator<f32>>& zPlanes,
    const f32 z
){
    if(!IsFinite(z))
        return false;
    for(const f32 existing : zPlanes){
        if(Abs(existing - z) <= s_OperatorFootprintPlaneEpsilon)
            return true;
    }
    zPlanes.push_back(z);
    return true;
}

[[nodiscard]] bool BuildOperatorProfilePlaneSample(
    const Vector<GeometryVertex>& vertices,
    const f32 z,
    const f32 minZ,
    const f32 maxZ,
    const f32 topRadius,
    DeformableOperatorProfileSample& outSample,
    Core::Alloc::ScratchArena<>& scratchArena
){
    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>> points{
        Core::Alloc::ScratchAllocator<OperatorFootprintPoint>(scratchArena)
    };
    points.reserve(vertices.size());

    const f32 planeEpsilon = Max(s_OperatorFootprintPlaneEpsilon, Abs(maxZ - minZ) * 0.00001f);
    for(const GeometryVertex& vertex : vertices){
        if(Abs(vertex.position.z - z) > planeEpsilon)
            continue;
        if(!AppendUniqueOperatorFootprintPoint(points, vertex.position.x, vertex.position.y))
            return false;
    }
    if(points.empty())
        return false;

    f32 centerX = 0.0f;
    f32 centerY = 0.0f;
    for(const OperatorFootprintPoint& point : points){
        centerX += point.x;
        centerY += point.y;
    }
    const f32 invPointCount = 1.0f / static_cast<f32>(points.size());
    centerX *= invPointCount;
    centerY *= invPointCount;

    f32 radius = 0.0f;
    for(const OperatorFootprintPoint& point : points){
        const f32 dx = point.x - centerX;
        const f32 dy = point.y - centerY;
        radius = Max(radius, VectorGetX(VectorSqrt(VectorReplicate((dx * dx) + (dy * dy)))));
    }

    const f32 depthSpan = maxZ - minZ;
    if(!IsFinite(depthSpan) || depthSpan <= s_OperatorProfileDepthEpsilon || topRadius <= s_OperatorProfileScaleEpsilon)
        return false;

    outSample.depth = (maxZ - z) / depthSpan;
    outSample.scale = radius / topRadius;
    outSample.center = Float2U(centerX, centerY);
    return
        IsFinite(outSample.depth)
        && IsFinite(outSample.scale)
        && IsFinite(outSample.center.x)
        && IsFinite(outSample.center.y)
    ;
}

[[nodiscard]] bool BuildOperatorProfileFromGeometryImpl(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorProfile& outProfile,
    Core::Alloc::ScratchArena<>& scratchArena
){
    outProfile = DeformableOperatorProfile{};
    if(vertices.empty())
        return false;

    f32 minZ = 0.0f;
    f32 maxZ = 0.0f;
    bool foundPosition = false;
    Vector<f32, Core::Alloc::ScratchAllocator<f32>> zPlanes{
        Core::Alloc::ScratchAllocator<f32>(scratchArena)
    };
    zPlanes.reserve(vertices.size());
    for(const GeometryVertex& vertex : vertices){
        if(!FiniteOperatorPosition(vertex.position))
            return false;
        if(!foundPosition){
            minZ = vertex.position.z;
            maxZ = vertex.position.z;
            foundPosition = true;
        }
        else{
            minZ = Min(minZ, vertex.position.z);
            maxZ = Max(maxZ, vertex.position.z);
        }
        if(!AppendUniqueOperatorProfileZ(zPlanes, vertex.position.z))
            return false;
    }
    if(!foundPosition)
        return false;

    if(maxZ - minZ <= s_OperatorProfileDepthEpsilon)
        return true;
    if(zPlanes.size() < 2u)
        return false;

    Sort(zPlanes.begin(), zPlanes.end(), [](const f32 lhs, const f32 rhs){
        return lhs > rhs;
    });

    DeformableOperatorProfileSample topSample;
    if(
        !BuildOperatorProfilePlaneSample(
            vertices,
            maxZ,
            minZ,
            maxZ,
            1.0f,
            topSample,
            scratchArena
        )
        || topSample.scale <= s_OperatorProfileScaleEpsilon
    )
        return false;
    const f32 topRadius = topSample.scale;

    const usize sourcePlaneCount = zPlanes.size();
    const usize sampleCount = Min(
        sourcePlaneCount,
        static_cast<usize>(s_DeformableOperatorProfileMaxSampleCount)
    );
    if(sampleCount < 2u)
        return false;

    for(usize sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex){
        const usize planeIndex = sampleIndex + 1u == sampleCount
            ? sourcePlaneCount - 1u
            : (sampleIndex * (sourcePlaneCount - 1u)) / (sampleCount - 1u)
        ;
        if(planeIndex >= zPlanes.size())
            return false;

        DeformableOperatorProfileSample sample;
        if(
            !BuildOperatorProfilePlaneSample(
                vertices,
                zPlanes[planeIndex],
                minZ,
                maxZ,
                topRadius,
                sample,
                scratchArena
            )
        )
            return false;

        if(sampleIndex == 0u){
            sample.depth = 0.0f;
            sample.scale = 1.0f;
        }
        else if(sampleIndex + 1u == sampleCount)
            sample.depth = 1.0f;

        outProfile.samples[outProfile.sampleCount] = sample;
        ++outProfile.sampleCount;
    }

    return ValidateOperatorProfile(outProfile);
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
        && ExactFloat3(lhs.position, rhs.position)
        && ExactFloat3(lhs.normal, rhs.normal)
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
        && depth >= 0.0f
    ;
}

[[nodiscard]] bool ValidateHoleShape(const DeformableHoleEditParams& params){
    return
        ValidateHoleShapeValues(params.radius, params.ellipseRatio, params.depth)
        && ValidateOperatorFootprint(params.operatorFootprint)
        && ValidateOperatorProfile(params.operatorProfile)
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
    return BuildHoleFrame(instance, hitTriangleIndices, hitBary, outFrame);
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

[[nodiscard]] bool ValidHoleEditResult(const DeformableHoleEditResult& result, const bool requireWall){
    if(result.editRevision == 0u || result.removedTriangleCount == 0u)
        return false;
    if(!ValidWallLoopCutCount(result.wallLoopCutCount))
        return false;
    if(!ValidOptionalWallVertexSpan(result.firstWallVertex, result.wallVertexCount))
        return false;
    if(result.wallVertexCount == 0u)
        return
            !requireWall
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
        ValidHoleEditResult(result, true)
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
        && ValidateOperatorFootprint(record.operatorFootprint)
        && ValidateOperatorProfile(record.operatorProfile)
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
    const f32 scale = VectorGetX(VectorSqrt(VectorReplicate((1.0f + dot) * 2.0f)));
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

struct BottomCapPolygonVertex{
    u32 vertex = 0u;
    f32 x = 0.0f;
    f32 y = 0.0f;
};

[[nodiscard]] f32 Cross2D(
    const BottomCapPolygonVertex& a,
    const BottomCapPolygonVertex& b,
    const BottomCapPolygonVertex& c
){
    return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

[[nodiscard]] f32 DistanceSq2D(const BottomCapPolygonVertex& a, const BottomCapPolygonVertex& b){
    const f32 dx = b.x - a.x;
    const f32 dy = b.y - a.y;
    return (dx * dx) + (dy * dy);
}

template<typename PolygonAllocator>
[[nodiscard]] f32 SignedArea2D(const Vector<BottomCapPolygonVertex, PolygonAllocator>& polygon){
    f32 area = 0.0f;
    for(usize vertexIndex = 0u; vertexIndex < polygon.size(); ++vertexIndex){
        const usize nextVertexIndex = (vertexIndex + 1u) % polygon.size();
        area +=
            (polygon[vertexIndex].x * polygon[nextVertexIndex].y)
            - (polygon[vertexIndex].y * polygon[nextVertexIndex].x)
        ;
    }
    return area * 0.5f;
}

[[nodiscard]] bool PointInsideTriangle2D(
    const BottomCapPolygonVertex& point,
    const BottomCapPolygonVertex& a,
    const BottomCapPolygonVertex& b,
    const BottomCapPolygonVertex& c,
    const bool counterClockwise
){
    constexpr f32 epsilon = 0.0000001f;
    const f32 ab = Cross2D(a, b, point);
    const f32 bc = Cross2D(b, c, point);
    const f32 ca = Cross2D(c, a, point);
    return
        counterClockwise
            ? ab >= -epsilon && bc >= -epsilon && ca >= -epsilon
            : ab <= epsilon && bc <= epsilon && ca <= epsilon
    ;
}

template<typename PolygonAllocator>
[[nodiscard]] bool RemoveDuplicateCapVertices(Vector<BottomCapPolygonVertex, PolygonAllocator>& polygon){
    constexpr f32 distanceEpsilonSq = 0.000000000001f;
    bool removed = true;
    while(removed && polygon.size() >= 3u){
        removed = false;
        for(usize vertexIndex = 0u; vertexIndex < polygon.size(); ++vertexIndex){
            const usize previousVertexIndex = vertexIndex == 0u ? polygon.size() - 1u : vertexIndex - 1u;
            const usize nextVertexIndex = (vertexIndex + 1u) % polygon.size();
            const BottomCapPolygonVertex& previous = polygon[previousVertexIndex];
            const BottomCapPolygonVertex& current = polygon[vertexIndex];
            const BottomCapPolygonVertex& next = polygon[nextVertexIndex];
            if(
                DistanceSq2D(previous, current) <= distanceEpsilonSq
                || DistanceSq2D(current, next) <= distanceEpsilonSq
            ){
                polygon.erase(polygon.begin() + static_cast<ptrdiff_t>(vertexIndex));
                removed = true;
                break;
            }
        }
    }
    return polygon.size() >= 3u;
}

template<typename IndexAllocator>
[[nodiscard]] bool AppendBottomCapTriangle(
    const BottomCapPolygonVertex& a,
    const BottomCapPolygonVertex& b,
    const BottomCapPolygonVertex& c,
    const bool counterClockwise,
    Vector<u32, IndexAllocator>& outIndices
){
    if(a.vertex == b.vertex || a.vertex == c.vertex || b.vertex == c.vertex)
        return false;

    outIndices.push_back(a.vertex);
    outIndices.push_back(counterClockwise ? b.vertex : c.vertex);
    outIndices.push_back(counterClockwise ? c.vertex : b.vertex);
    return true;
}

template<typename PolygonAllocator>
[[nodiscard]] bool IsBottomCapEar(
    const Vector<BottomCapPolygonVertex, PolygonAllocator>& polygon,
    const usize vertexIndex,
    const bool counterClockwise
){
    const usize previousVertexIndex = vertexIndex == 0u ? polygon.size() - 1u : vertexIndex - 1u;
    const usize nextVertexIndex = (vertexIndex + 1u) % polygon.size();
    const BottomCapPolygonVertex& previous = polygon[previousVertexIndex];
    const BottomCapPolygonVertex& current = polygon[vertexIndex];
    const BottomCapPolygonVertex& next = polygon[nextVertexIndex];
    const f32 cross = Cross2D(previous, current, next);
    if(
        counterClockwise
            ? cross <= s_BottomCapTriangulationAreaEpsilon
            : cross >= -s_BottomCapTriangulationAreaEpsilon
    )
        return false;

    for(usize testVertexIndex = 0u; testVertexIndex < polygon.size(); ++testVertexIndex){
        if(
            testVertexIndex == previousVertexIndex
            || testVertexIndex == vertexIndex
            || testVertexIndex == nextVertexIndex
        )
            continue;

        if(PointInsideTriangle2D(polygon[testVertexIndex], previous, current, next, counterClockwise))
            return false;
    }
    return true;
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
        || ((capVertices.size() - 2u) * 3u) > Limit<usize>::s_Max - outIndices.size()
    )
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<BottomCapPolygonVertex, Core::Alloc::ScratchAllocator<BottomCapPolygonVertex>> polygon{
        Core::Alloc::ScratchAllocator<BottomCapPolygonVertex>(scratchArena)
    };
    polygon.reserve(capVertices.size());
    const SIMDVector origin = LoadRestVertexPosition(restVertices[capVertices[0u]]);
    for(const u32 capVertex : capVertices){
        if(capVertex >= restVertices.size())
            return false;

        const SIMDVector offset = VectorSubtract(LoadRestVertexPosition(restVertices[capVertex]), origin);
        BottomCapPolygonVertex polygonVertex;
        polygonVertex.vertex = capVertex;
        polygonVertex.x = VectorGetX(Vector3Dot(offset, tangent));
        polygonVertex.y = VectorGetX(Vector3Dot(offset, bitangent));
        if(!IsFinite(polygonVertex.x) || !IsFinite(polygonVertex.y))
            return false;

        polygon.push_back(polygonVertex);
    }

    if(!RemoveDuplicateCapVertices(polygon))
        return false;

    const f32 signedArea = SignedArea2D(polygon);
    if(!IsFinite(signedArea) || Abs(signedArea) <= s_BottomCapTriangulationAreaEpsilon)
        return false;

    const bool counterClockwise = signedArea > 0.0f;
    const usize capTriangleCount = polygon.size() - 2u;
    outIndices.reserve(outIndices.size() + capTriangleCount * 3u);

    u32 addedTriangleCount = 0u;
    while(polygon.size() > 3u){
        bool clippedEar = false;
        for(usize vertexIndex = 0u; vertexIndex < polygon.size(); ++vertexIndex){
            if(!IsBottomCapEar(polygon, vertexIndex, counterClockwise))
                continue;

            const usize previousVertexIndex = vertexIndex == 0u ? polygon.size() - 1u : vertexIndex - 1u;
            const usize nextVertexIndex = (vertexIndex + 1u) % polygon.size();
            if(
                !AppendBottomCapTriangle(
                    polygon[previousVertexIndex],
                    polygon[vertexIndex],
                    polygon[nextVertexIndex],
                    counterClockwise,
                    outIndices
                )
            )
                return false;

            polygon.erase(polygon.begin() + static_cast<ptrdiff_t>(vertexIndex));
            addedTriangleCount += 1u;
            clippedEar = true;
            break;
        }

        if(!clippedEar)
            return false;
    }

    if(
        !AppendBottomCapTriangle(
            polygon[0u],
            polygon[1u],
            polygon[2u],
            counterClockwise,
            outIndices
        )
    )
        return false;
    addedTriangleCount += 1u;

    if(addedTriangleCount != capTriangleCount || addedTriangleCount > static_cast<u32>(Limit<u32>::s_Max))
        return false;

    if(outAddedTriangleCount)
        *outAddedTriangleCount = addedTriangleCount;
    return true;
}

[[nodiscard]] bool PointOnOperatorFootprintEdge(
    const f32 x,
    const f32 y,
    const Float2U& a,
    const Float2U& b
){
    constexpr f32 epsilon = 0.000001f;
    const f32 edgeX = b.x - a.x;
    const f32 edgeY = b.y - a.y;
    const f32 pointX = x - a.x;
    const f32 pointY = y - a.y;
    const f32 cross = (pointX * edgeY) - (pointY * edgeX);
    if(Abs(cross) > epsilon)
        return false;

    const f32 dot = (pointX * edgeX) + (pointY * edgeY);
    if(dot < -epsilon)
        return false;

    const f32 edgeLengthSq = (edgeX * edgeX) + (edgeY * edgeY);
    return dot <= edgeLengthSq + epsilon;
}

[[nodiscard]] bool PointInsideOperatorFootprint(
    const DeformableOperatorFootprint& footprint,
    const f32 x,
    const f32 y
){
    bool inside = false;
    u32 previous = footprint.vertexCount - 1u;
    for(u32 current = 0u; current < footprint.vertexCount; ++current){
        const Float2U& a = footprint.vertices[current];
        const Float2U& b = footprint.vertices[previous];
        if(PointOnOperatorFootprintEdge(x, y, a, b))
            return true;

        const bool crossesY = (a.y > y) != (b.y > y);
        if(crossesY){
            const f32 intersectX = a.x + ((y - a.y) * (b.x - a.x) / (b.y - a.y));
            if(x <= intersectX + 0.000001f)
                inside = !inside;
        }
        previous = current;
    }
    return inside;
}

[[nodiscard]] bool SampleOperatorProfile(
    const DeformableOperatorProfile& profile,
    const f32 rawDepth,
    Float2U& outCenter,
    f32& outScale
){
    outCenter = Float2U(0.0f, 0.0f);
    outScale = 1.0f;
    if(profile.sampleCount == 0u)
        return true;
    if(!ValidateOperatorProfile(profile) || !IsFinite(rawDepth))
        return false;

    const f32 depth = Min(Max(rawDepth, 0.0f), 1.0f);
    if(depth <= profile.samples[0u].depth + s_OperatorProfileDepthEpsilon){
        outCenter = profile.samples[0u].center;
        outScale = profile.samples[0u].scale;
        return true;
    }

    for(u32 i = 1u; i < profile.sampleCount; ++i){
        const DeformableOperatorProfileSample& previous = profile.samples[i - 1u];
        const DeformableOperatorProfileSample& next = profile.samples[i];
        if(depth > next.depth + s_OperatorProfileDepthEpsilon)
            continue;

        const f32 depthSpan = next.depth - previous.depth;
        if(depthSpan <= s_OperatorProfileDepthEpsilon)
            return false;

        const f32 t = (depth - previous.depth) / depthSpan;
        outCenter = Float2U(
            previous.center.x + ((next.center.x - previous.center.x) * t),
            previous.center.y + ((next.center.y - previous.center.y) * t)
        );
        outScale = previous.scale + ((next.scale - previous.scale) * t);
        return IsFinite(outCenter.x) && IsFinite(outCenter.y) && IsFinite(outScale);
    }

    const DeformableOperatorProfileSample& last = profile.samples[profile.sampleCount - 1u];
    outCenter = last.center;
    outScale = last.scale;
    return true;
}

[[nodiscard]] bool PointInsideOperatorCrossSection(
    const DeformableOperatorFootprint& operatorFootprint,
    const f32 x,
    const f32 y
){
    if(operatorFootprint.vertexCount != 0u)
        return PointInsideOperatorFootprint(operatorFootprint, x, y);

    return ((x * x) + (y * y)) <= 1.0f;
}

[[nodiscard]] bool PointInsideOperatorVolume(
    const HoleFrame& frame,
    const DeformableHoleEditParams& params,
    const SIMDVector point
){
    const f32 radiusX = params.radius;
    const f32 radiusY = params.radius * params.ellipseRatio;
    if(radiusX <= s_Epsilon || radiusY <= s_Epsilon)
        return false;

    const SIMDVector offset = VectorSubtract(point, frame.center);
    const f32 localX = VectorGetX(Vector3Dot(offset, frame.tangent)) / radiusX;
    const f32 localY = VectorGetX(Vector3Dot(offset, frame.bitangent)) / radiusY;
    if(!IsFinite(localX) || !IsFinite(localY))
        return false;

    if(params.depth <= s_Epsilon)
        return PointInsideOperatorCrossSection(params.operatorFootprint, localX, localY);

    const f32 normalizedDepth = -VectorGetX(Vector3Dot(offset, frame.normal)) / params.depth;
    if(
        !IsFinite(normalizedDepth)
        || normalizedDepth < -s_OperatorProfileDepthEpsilon
        || normalizedDepth > 1.0f + s_OperatorProfileDepthEpsilon
    )
        return false;

    Float2U center;
    f32 scale = 1.0f;
    if(!SampleOperatorProfile(params.operatorProfile, normalizedDepth, center, scale))
        return false;
    if(!IsFinite(scale) || scale < 0.0f)
        return false;

    if(scale <= s_OperatorProfileScaleEpsilon){
        const f32 dx = localX - center.x;
        const f32 dy = localY - center.y;
        return ((dx * dx) + (dy * dy)) <= (s_OperatorProfileScaleEpsilon * s_OperatorProfileScaleEpsilon);
    }

    const Float2U topCenter = params.operatorProfile.sampleCount != 0u
        ? params.operatorProfile.samples[0u].center
        : Float2U(0.0f, 0.0f)
    ;
    const f32 topX = topCenter.x + ((localX - center.x) / scale);
    const f32 topY = topCenter.y + ((localY - center.y) / scale);
    if(!IsFinite(topX) || !IsFinite(topY))
        return false;

    return PointInsideOperatorCrossSection(params.operatorFootprint, topX, topY);
}

[[nodiscard]] bool TriangleCentroidInsideOperatorVolume(
    const DeformableRuntimeMeshInstance& instance,
    const HoleFrame& frame,
    const DeformableHoleEditParams& params,
    const u32 (&triangleIndices)[3]
){
    return PointInsideOperatorVolume(frame, params, TriangleCentroid(instance, triangleIndices));
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
    return params.operatorFootprint.vertexCount >= 3u && params.depth > DeformableRuntime::s_Epsilon;
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

[[nodiscard]] bool SurfaceRemeshKeepHalfPlanePoint(const f32 distance, const bool keepInside){
    return keepInside
        ? distance >= -s_SurfaceRemeshClipEpsilon
        : distance <= s_SurfaceRemeshClipEpsilon
    ;
}

[[nodiscard]] f32 SurfaceRemeshPointDistanceSq(
    const SurfaceRemeshClipPoint& lhs,
    const SurfaceRemeshClipPoint& rhs
){
    const f32 dx = rhs.local.x - lhs.local.x;
    const f32 dy = rhs.local.y - lhs.local.y;
    return (dx * dx) + (dy * dy);
}

[[nodiscard]] bool NormalizeSurfaceRemeshClipPoint(SurfaceRemeshClipPoint& point){
    if(
        !IsFinite(point.local.x)
        || !IsFinite(point.local.y)
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
    for(u32 i = 0u; i < 3u; ++i)
        point.bary[i] = a.bary[i] + ((b.bary[i] - a.bary[i]) * t);
    point.originalVertex = Limit<u32>::s_Max;
    return point;
}

[[nodiscard]] bool ClipSurfaceRemeshPolygonHalfPlane(
    const SurfaceRemeshClipPolygon& input,
    const Float2U& edgeA,
    const Float2U& edgeB,
    const f32 orientation,
    const bool keepInside,
    SurfaceRemeshClipPolygon& output
){
    output.clear();
    if(input.empty())
        return true;

    SurfaceRemeshClipPoint previous = input.back();
    f32 previousDistance = SurfaceRemeshHalfPlaneDistance(edgeA, edgeB, previous, orientation);
    bool previousKept = SurfaceRemeshKeepHalfPlanePoint(previousDistance, keepInside);
    for(const SurfaceRemeshClipPoint& current : input){
        const f32 currentDistance = SurfaceRemeshHalfPlaneDistance(edgeA, edgeB, current, orientation);
        const bool currentKept = SurfaceRemeshKeepHalfPlanePoint(currentDistance, keepInside);
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

[[nodiscard]] f32 SurfaceRemeshPolygonSignedArea(const SurfaceRemeshClipPolygon& polygon){
    f32 area = 0.0f;
    for(usize i = 0u; i < polygon.size(); ++i){
        const usize next = (i + 1u) % polygon.size();
        area +=
            (polygon[i].local.x * polygon[next].local.y)
            - (polygon[i].local.y * polygon[next].local.x)
        ;
    }
    return area * 0.5f;
}

[[nodiscard]] bool RemoveCollinearSurfaceRemeshClipPoints(SurfaceRemeshClipPolygon& polygon){
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
            const f32 cross =
                ((b.local.x - a.local.x) * (c.local.y - a.local.y))
                - ((b.local.y - a.local.y) * (c.local.x - a.local.x))
            ;
            if(
                abDistanceSq <= s_SurfaceRemeshVertexMergeDistanceSq
                || bcDistanceSq <= s_SurfaceRemeshVertexMergeDistanceSq
                || Abs(cross) <= s_SurfaceRemeshAreaEpsilon
            ){
                polygon.erase(polygon.begin() + static_cast<ptrdiff_t>(i));
                removed = true;
                break;
            }
        }
    }

    return polygon.size() >= 3u && Abs(SurfaceRemeshPolygonSignedArea(polygon)) > s_SurfaceRemeshAreaEpsilon;
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
    if(!IsFinite(localX) || !IsFinite(localY))
        return false;

    outPoint = SurfaceRemeshClipPoint{};
    outPoint.local = Float2U(localX, localY);
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

    Float3U storedPosition;
    StoreFloat(position, &storedPosition);
    for(const SurfaceRemeshGeneratedVertex& generated : generatedVertices){
        const f32 localDx = generated.local.x - point.local.x;
        const f32 localDy = generated.local.y - point.local.y;
        const SIMDVector generatedDelta = VectorSubtract(LoadFloat(generated.position), position);
        if(
            (localDx * localDx) + (localDy * localDy) <= s_SurfaceRemeshVertexMergeDistanceSq
            && VectorGetX(Vector3LengthSq(generatedDelta)) <= s_SurfaceRemeshVertexMergeDistanceSq
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
    for(const SurfaceRemeshClipPoint& point : polygon){
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
        maxDepth >= -s_OperatorProfileDepthEpsilon
        && minDepth <= 1.0f + s_OperatorProfileDepthEpsilon
    ;
    return true;
}

[[nodiscard]] bool SplitSurfaceRemeshTrianglesAtGeneratedEdgeVertices(
    const Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& restPositions,
    const Vector<SurfaceRemeshGeneratedVertex, Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>>& generatedVertices,
    Vector<SurfaceRemeshTriangle, Core::Alloc::ScratchAllocator<SurfaceRemeshTriangle>>& surfaceTriangles
){
    if(generatedVertices.empty() || surfaceTriangles.empty())
        return true;

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
                for(const SurfaceRemeshGeneratedVertex& generated : generatedVertices){
                    const u32 splitVertex = generated.vertex;
                    if(!SurfaceRemeshEdgeContainsVertex(restPositions, edgeA, edgeB, splitVertex))
                        continue;
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
    Vector<u8, Core::Alloc::ScratchAllocator<u8>>& outAffectedTriangles,
    u32& outAffectedTriangleCount,
    Core::Alloc::ScratchArena<>& scratchArena
){
    outFrame = HoleFrame{};
    outOrderedBoundaryEdges.clear();
    outRestPositions.clear();
    outSurfaceTriangles.clear();
    outGeneratedVertices.clear();
    outAffectedTriangles.clear();
    outAffectedTriangleCount = 0u;

    if(!UseOperatorSurfaceRemesh(params) || !ValidateOperatorFootprint(params.operatorFootprint))
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
    if(!BuildHoleFrame(instance, hitTriangleIndices, hitBary, outFrame))
        return false;

    if(!PointInsideOperatorCrossSection(params.operatorFootprint, 0.0f, 0.0f))
        return false;

    outRestPositions.reserve(instance.restVertices.size());
    for(const DeformableVertexRest& vertex : instance.restVertices)
        outRestPositions.push_back(vertex.position);

    outAffectedTriangles.resize(triangleCount, 0u);

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
    boundaryEdgeMap.reserve(triangleCount * 3u);

    const f32 footprintArea = OperatorFootprintSignedArea(params.operatorFootprint);
    if(!IsFinite(footprintArea) || Abs(footprintArea) <= s_OperatorFootprintAreaEpsilon)
        return false;
    const f32 orientation = footprintArea >= 0.0f ? 1.0f : -1.0f;

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
            if(
                !AppendSurfaceRemeshTriangle(
                    outRestPositions,
                    triangleNormal,
                    triangleIndices[0u],
                    triangleIndices[1u],
                    triangleIndices[2u],
                    static_cast<u32>(triangle),
                    outSurfaceTriangles
                )
            )
                return false;
            continue;
        }

        SurfaceRemeshClipPolygon active{
            Core::Alloc::ScratchAllocator<SurfaceRemeshClipPoint>(scratchArena)
        };
        active.reserve(8u);
        for(u32 vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex){
            SurfaceRemeshClipPoint point;
            if(
                !BuildSurfaceRemeshTriangleClipPoint(
                    instance,
                    outFrame,
                    params,
                    triangleIndices[vertexIndex],
                    vertexIndex,
                    point
                )
                || !AppendSurfaceRemeshClipPoint(active, point)
            )
                return false;
        }

        SurfaceRemeshClipPolygonList outsidePieces{
            Core::Alloc::ScratchAllocator<SurfaceRemeshClipPolygon>(scratchArena)
        };
        outsidePieces.reserve(params.operatorFootprint.vertexCount);
        bool clippedAway = false;
        for(u32 edgeIndex = 0u; edgeIndex < params.operatorFootprint.vertexCount; ++edgeIndex){
            const u32 nextEdgeIndex = (edgeIndex + 1u) % params.operatorFootprint.vertexCount;
            const Float2U& edgeA = params.operatorFootprint.vertices[edgeIndex];
            const Float2U& edgeB = params.operatorFootprint.vertices[nextEdgeIndex];

            SurfaceRemeshClipPolygon outside{
                Core::Alloc::ScratchAllocator<SurfaceRemeshClipPoint>(scratchArena)
            };
            SurfaceRemeshClipPolygon inside{
                Core::Alloc::ScratchAllocator<SurfaceRemeshClipPoint>(scratchArena)
            };
            outside.reserve(active.size() + 1u);
            inside.reserve(active.size() + 1u);
            if(
                !ClipSurfaceRemeshPolygonHalfPlane(active, edgeA, edgeB, orientation, false, outside)
                || !ClipSurfaceRemeshPolygonHalfPlane(active, edgeA, edgeB, orientation, true, inside)
            )
                return false;

            if(RemoveCollinearSurfaceRemeshClipPoints(outside)){
                outsidePieces.emplace_back(Core::Alloc::ScratchAllocator<SurfaceRemeshClipPoint>(scratchArena));
                outsidePieces.back().assign(outside.begin(), outside.end());
            }

            if(!RemoveCollinearSurfaceRemeshClipPoints(inside)){
                clippedAway = true;
                break;
            }

            active.assign(inside.begin(), inside.end());
        }

        if(clippedAway || !RemoveCollinearSurfaceRemeshClipPoints(active)){
            if(
                !AppendSurfaceRemeshTriangle(
                    outRestPositions,
                    triangleNormal,
                    triangleIndices[0u],
                    triangleIndices[1u],
                    triangleIndices[2u],
                    static_cast<u32>(triangle),
                    outSurfaceTriangles
                )
            )
                return false;
            continue;
        }

        outAffectedTriangles[triangle] = 1u;
        ++outAffectedTriangleCount;

        Vector<u32, Core::Alloc::ScratchAllocator<u32>> insideVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        insideVertices.reserve(active.size());
        for(const SurfaceRemeshClipPoint& point : active){
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
        for(usize vertexIndex = 0u; vertexIndex < insideVertices.size(); ++vertexIndex){
            const usize nextVertexIndex = (vertexIndex + 1u) % insideVertices.size();
            if(!RegisterSurfaceRemeshBoundaryEdge(boundaryEdgeMap, insideVertices[vertexIndex], insideVertices[nextVertexIndex]))
                return false;
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

    if(!SplitSurfaceRemeshTrianglesAtGeneratedEdgeVertices(outRestPositions, outGeneratedVertices, outSurfaceTriangles))
        return false;

    if(
        outAffectedTriangleCount == 0u
        || outAffectedTriangleCount >= static_cast<u32>(triangleCount)
        || params.posedHit.triangle >= outAffectedTriangles.size()
        || outAffectedTriangles[params.posedHit.triangle] == 0u
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
    if(params.operatorProfile.sampleCount == 0u)
        return true;
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

[[nodiscard]] bool BoundaryKeptFacesSupportHoleExtrusion(
    const DeformableRuntimeMeshInstance& instance,
    const SIMDVector frameNormal,
    const Vector<u8, Core::Alloc::ScratchAllocator<u8>>& removeTriangle,
    const Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>>& orderedBoundaryEdges
){
    const usize triangleCount = instance.indices.size() / 3u;
    if(
        instance.indices.empty()
        || (instance.indices.size() % 3u) != 0u
        || removeTriangle.size() != triangleCount
        || orderedBoundaryEdges.empty()
        || !FiniteVec3(frameNormal)
    )
        return false;

    usize keptBoundaryEdgeCount = 0u;
    usize compatibleKeptBoundaryEdgeCount = 0u;
    for(const EdgeRecord& boundaryEdge : orderedBoundaryEdges){
        for(usize triangle = 0u; triangle < triangleCount; ++triangle){
            if(removeTriangle[triangle] != 0u)
                continue;

            const usize indexBase = triangle * 3u;
            const u32 triangleIndices[3] = {
                instance.indices[indexBase + 0u],
                instance.indices[indexBase + 1u],
                instance.indices[indexBase + 2u],
            };
            if(!TriangleContainsEdge(triangleIndices, boundaryEdge.a, boundaryEdge.b))
                continue;

            SIMDVector keptNormal;
            if(!BuildTriangleNormal(instance, triangleIndices, keptNormal))
                return false;

            const f32 normalDot = Abs(VectorGetX(Vector3Dot(frameNormal, keptNormal)));
            if(!IsFinite(normalDot))
                return false;
            ++keptBoundaryEdgeCount;
            if(normalDot >= s_MinHoleBoundaryKeptFaceNormalDot)
                ++compatibleKeptBoundaryEdgeCount;

            break;
        }
    }
    // A boundary made only of hard kept faces is a coarse whole-face cut, not a usable inset hole.
    return keptBoundaryEdgeCount == 0u || compatibleKeptBoundaryEdgeCount != 0u;
}

[[nodiscard]] u64 MakeTriangleEdgeKey(const u32 a, const u32 b){
    const u32 lo = a < b ? a : b;
    const u32 hi = a < b ? b : a;
    return (static_cast<u64>(lo) << 32u) | static_cast<u64>(hi);
}

struct TriangleCandidateEdge{
    u32 triangle = Limit<u32>::s_Max;
    bool paired = false;
};

struct EdgeAdjacencyEntry{
    u32 edgeIndex = Limit<u32>::s_Max;
    u32 next = Limit<u32>::s_Max;
};
static_assert(IsStandardLayout_V<EdgeAdjacencyEntry>, "EdgeAdjacencyEntry must stay layout-stable");
static_assert(IsTriviallyCopyable_V<EdgeAdjacencyEntry>, "EdgeAdjacencyEntry must stay cheap to copy");

[[nodiscard]] bool EdgeTouchesRemovedVertex(
    const EdgeRecord& edge,
    const Vector<u8, Core::Alloc::ScratchAllocator<u8>>& removedVertices
){
    return
        edge.a < removedVertices.size()
        && edge.b < removedVertices.size()
        && (removedVertices[edge.a] != 0u || removedVertices[edge.b] != 0u)
    ;
}

[[nodiscard]] bool EdgeLooksLikeExistingCsgWallBoundary(
    const EdgeRecord& edge,
    const Vector<DeformableVertexRest>& restVertices,
    const SIMDVector frameNormal
){
    if(
        edge.fullCount != 1u
        || edge.removedCount != 0u
        || edge.a >= restVertices.size()
        || edge.b >= restVertices.size()
    )
        return false;

    const f32 aNormalDot = Abs(VectorGetX(Vector3Dot(LoadFloat(restVertices[edge.a].normal), frameNormal)));
    const f32 bNormalDot = Abs(VectorGetX(Vector3Dot(LoadFloat(restVertices[edge.b].normal), frameNormal)));
    return
        IsFinite(aNormalDot)
        && IsFinite(bNormalDot)
        && (aNormalDot < 0.5f || bNormalDot < 0.5f)
    ;
}

[[nodiscard]] bool AppendTriangleNeighbor(
    Vector<u32, Core::Alloc::ScratchAllocator<u32>>& neighbors,
    Vector<u8, Core::Alloc::ScratchAllocator<u8>>& neighborCounts,
    const u32 triangle,
    const u32 neighbor
){
    if(
        triangle >= neighborCounts.size()
        || neighbor >= neighborCounts.size()
        || neighborCounts[triangle] >= 3u
        || triangle > (Limit<u32>::s_Max / 3u)
    )
        return false;

    const usize offset = static_cast<usize>(triangle) * 3u + neighborCounts[triangle];
    if(offset >= neighbors.size())
        return false;

    neighbors[offset] = neighbor;
    ++neighborCounts[triangle];
    return true;
}

[[nodiscard]] bool BuildCandidateTriangleAdjacency(
    const Vector<u32>& indices,
    const Vector<u8, Core::Alloc::ScratchAllocator<u8>>& candidateTriangle,
    Vector<u32, Core::Alloc::ScratchAllocator<u32>>& neighbors,
    Vector<u8, Core::Alloc::ScratchAllocator<u8>>& neighborCounts,
    Core::Alloc::ScratchArena<>& scratchArena
){
    const usize triangleCount = candidateTriangle.size();
    if(indices.size() / 3u != triangleCount || triangleCount > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    usize candidateCount = 0u;
    for(const u8 flag : candidateTriangle){
        if(flag != 0u)
            ++candidateCount;
    }
    if(candidateCount > Limit<usize>::s_Max / 3u)
        return false;

    neighbors.assign(triangleCount * 3u, Limit<u32>::s_Max);
    neighborCounts.assign(triangleCount, 0u);

    using CandidateEdgeMap = HashMap<
        u64,
        TriangleCandidateEdge,
        Hasher<u64>,
        EqualTo<u64>,
        Core::Alloc::ScratchAllocator<Pair<const u64, TriangleCandidateEdge>>
    >;
    CandidateEdgeMap candidateEdges(
        0,
        Hasher<u64>(),
        EqualTo<u64>(),
        Core::Alloc::ScratchAllocator<Pair<const u64, TriangleCandidateEdge>>(scratchArena)
    );
    candidateEdges.reserve(candidateCount * 3u);

    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        if(candidateTriangle[triangle] == 0u)
            continue;

        const usize indexBase = triangle * 3u;
        const u32 triangleVertices[3] = {
            indices[indexBase + 0u],
            indices[indexBase + 1u],
            indices[indexBase + 2u],
        };
        if(
            triangleVertices[0] == triangleVertices[1]
            || triangleVertices[0] == triangleVertices[2]
            || triangleVertices[1] == triangleVertices[2]
        )
            return false;

        for(usize edgeIndex = 0u; edgeIndex < 3u; ++edgeIndex){
            const u32 a = triangleVertices[edgeIndex];
            const u32 b = triangleVertices[(edgeIndex + 1u) % 3u];
            auto [it, inserted] = candidateEdges.emplace(MakeTriangleEdgeKey(a, b), TriangleCandidateEdge{});
            TriangleCandidateEdge& edge = it.value();
            if(inserted){
                edge.triangle = static_cast<u32>(triangle);
                continue;
            }
            if(edge.paired || edge.triangle == Limit<u32>::s_Max)
                return false;

            const u32 currentTriangle = static_cast<u32>(triangle);
            if(
                !AppendTriangleNeighbor(neighbors, neighborCounts, currentTriangle, edge.triangle)
                || !AppendTriangleNeighbor(neighbors, neighborCounts, edge.triangle, currentTriangle)
            )
                return false;

            edge.paired = true;
        }
    }
    return true;
}

[[nodiscard]] bool KeepConnectedCandidateTriangles(
    const Vector<u32>& indices,
    const u32 hitTriangle,
    const Vector<u8, Core::Alloc::ScratchAllocator<u8>>& candidateTriangle,
    Vector<u8, Core::Alloc::ScratchAllocator<u8>>& removeTriangle,
    Core::Alloc::ScratchArena<>& scratchArena
){
    removeTriangle.assign(candidateTriangle.size(), 0u);
    if(hitTriangle >= candidateTriangle.size() || candidateTriangle[hitTriangle] == 0u)
        return false;

    usize candidateCount = 0u;
    for(const u8 flag : candidateTriangle){
        if(flag != 0u)
            ++candidateCount;
    }

    Vector<u32, Core::Alloc::ScratchAllocator<u32>> neighbors{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> neighborCounts{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    if(!BuildCandidateTriangleAdjacency(indices, candidateTriangle, neighbors, neighborCounts, scratchArena))
        return false;

    Vector<u32, Core::Alloc::ScratchAllocator<u32>> pendingTriangles{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
    pendingTriangles.reserve(candidateCount);
    removeTriangle[hitTriangle] = 1u;
    pendingTriangles.push_back(hitTriangle);

    for(usize pendingIndex = 0u; pendingIndex < pendingTriangles.size(); ++pendingIndex){
        const u32 triangle = pendingTriangles[pendingIndex];
        if(triangle >= neighborCounts.size())
            return false;

        const usize neighborOffset = static_cast<usize>(triangle) * 3u;
        for(u8 neighborIndex = 0u; neighborIndex < neighborCounts[triangle]; ++neighborIndex){
            const u32 neighbor = neighbors[neighborOffset + neighborIndex];
            if(neighbor >= candidateTriangle.size())
                return false;
            if(candidateTriangle[neighbor] == 0u || removeTriangle[neighbor] != 0u)
                continue;

            removeTriangle[neighbor] = 1u;
            pendingTriangles.push_back(neighbor);
        }
    }
    return true;
}

[[nodiscard]] bool BuildOpenBoundaryComponentFromRemovedTriangles(
    const Vector<u32>& indices,
    const Vector<DeformableVertexRest>& restVertices,
    const SIMDVector frameNormal,
    const Vector<u8, Core::Alloc::ScratchAllocator<u8>>& removedTriangles,
    Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>>& outBoundaryEdges,
    u32* outRemovedTriangleCount,
    Core::Alloc::ScratchArena<>& scratchArena
){
    if(outRemovedTriangleCount)
        *outRemovedTriangleCount = 0u;
    outBoundaryEdges.clear();

    if(
        indices.empty()
        || (indices.size() % 3u) != 0u
        || indices.size() / 3u != removedTriangles.size()
        || restVertices.empty()
        || !FiniteVec3(frameNormal)
    )
        return false;

    const usize vertexCount = restVertices.size();
    using EdgeRecordMap = HashMap<
        u64,
        EdgeRecord,
        Hasher<u64>,
        EqualTo<u64>,
        Core::Alloc::ScratchAllocator<Pair<const u64, EdgeRecord>>
    >;
    EdgeRecordMap edgeRecords(
        0,
        Hasher<u64>(),
        EqualTo<u64>(),
        Core::Alloc::ScratchAllocator<Pair<const u64, EdgeRecord>>(scratchArena)
    );
    edgeRecords.reserve(indices.size());

    Vector<u8, Core::Alloc::ScratchAllocator<u8>> removedVertices{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    removedVertices.resize(vertexCount, 0u);

    const usize triangleCount = indices.size() / 3u;
    u32 removedTriangleCount = 0u;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        const usize indexBase = triangle * 3u;
        const u32 triangleVertices[3] = {
            indices[indexBase + 0u],
            indices[indexBase + 1u],
            indices[indexBase + 2u],
        };
        if(
            triangleVertices[0u] >= vertexCount
            || triangleVertices[1u] >= vertexCount
            || triangleVertices[2u] >= vertexCount
            || triangleVertices[0u] == triangleVertices[1u]
            || triangleVertices[0u] == triangleVertices[2u]
            || triangleVertices[1u] == triangleVertices[2u]
        )
            return false;

        const bool removedTriangle = removedTriangles[triangle] != 0u;
        if(removedTriangle){
            ++removedTriangleCount;
            removedVertices[triangleVertices[0u]] = 1u;
            removedVertices[triangleVertices[1u]] = 1u;
            removedVertices[triangleVertices[2u]] = 1u;
        }

        for(usize edgeIndex = 0u; edgeIndex < 3u; ++edgeIndex){
            const u32 a = triangleVertices[edgeIndex];
            const u32 b = triangleVertices[(edgeIndex + 1u) % 3u];
            auto [it, inserted] = edgeRecords.emplace(MakeTriangleEdgeKey(a, b), EdgeRecord{});
            EdgeRecord& edge = it.value();
            if(inserted){
                edge.a = a;
                edge.b = b;
            }
            ++edge.fullCount;
            if(removedTriangle)
                ++edge.removedCount;
        }
    }
    if(removedTriangleCount == 0u || removedTriangleCount >= triangleCount)
        return false;

    auto fail = [&outBoundaryEdges](){
        outBoundaryEdges.clear();
        return false;
    };

    Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>> finalBoundaryEdges{
        Core::Alloc::ScratchAllocator<EdgeRecord>(scratchArena)
    };
    finalBoundaryEdges.reserve(edgeRecords.size());
    for(const auto& [edgeKey, edge] : edgeRecords){
        static_cast<void>(edgeKey);
        if(edge.fullCount == 0u || edge.fullCount > 2u || edge.removedCount > edge.fullCount)
            return fail();

        const u32 keptCount = edge.fullCount - edge.removedCount;
        if(keptCount == 1u)
            finalBoundaryEdges.push_back(edge);
    }
    if(finalBoundaryEdges.empty())
        return fail();
    if(finalBoundaryEdges.size() > static_cast<usize>(Limit<u32>::s_Max) / 2u)
        return fail();
    outBoundaryEdges.reserve(finalBoundaryEdges.size());

    using EdgeFirstMap = HashMap<
        u32,
        u32,
        Hasher<u32>,
        EqualTo<u32>,
        Core::Alloc::ScratchAllocator<Pair<const u32, u32>>
    >;
    EdgeFirstMap firstEdgeByVertex(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        Core::Alloc::ScratchAllocator<Pair<const u32, u32>>(scratchArena)
    );
    firstEdgeByVertex.reserve(finalBoundaryEdges.size() * 2u);

    Vector<EdgeAdjacencyEntry, Core::Alloc::ScratchAllocator<EdgeAdjacencyEntry>> edgeAdjacency{
        Core::Alloc::ScratchAllocator<EdgeAdjacencyEntry>(scratchArena)
    };
    edgeAdjacency.reserve(finalBoundaryEdges.size() * 2u);

    auto appendEdgeAdjacency = [&](
        const u32 vertex,
        const u32 edgeIndex
    ) -> bool{
        if(vertex >= vertexCount || edgeAdjacency.size() >= static_cast<usize>(Limit<u32>::s_Max))
            return false;

        auto [firstIt, inserted] = firstEdgeByVertex.emplace(vertex, Limit<u32>::s_Max);
        const u32 previousFirst = inserted ? Limit<u32>::s_Max : firstIt.value();
        firstIt.value() = static_cast<u32>(edgeAdjacency.size());
        edgeAdjacency.push_back(EdgeAdjacencyEntry{ edgeIndex, previousFirst });
        return true;
    };
    for(usize edgeIndex = 0u; edgeIndex < finalBoundaryEdges.size(); ++edgeIndex){
        const EdgeRecord& edge = finalBoundaryEdges[edgeIndex];
        const u32 edgeIndex32 = static_cast<u32>(edgeIndex);
        if(!appendEdgeAdjacency(edge.a, edgeIndex32) || !appendEdgeAdjacency(edge.b, edgeIndex32))
            return fail();
    }

    Vector<u8, Core::Alloc::ScratchAllocator<u8>> visitedEdges{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    visitedEdges.resize(finalBoundaryEdges.size(), 0u);

    Vector<usize, Core::Alloc::ScratchAllocator<usize>> pendingEdges{
        Core::Alloc::ScratchAllocator<usize>(scratchArena)
    };
    pendingEdges.reserve(finalBoundaryEdges.size());

    bool foundTouchedComponent = false;
    bool foundExistingCsgWallBoundary = false;
    for(usize edgeIndex = 0u; edgeIndex < finalBoundaryEdges.size(); ++edgeIndex){
        if(
            visitedEdges[edgeIndex] != 0u
            || !EdgeTouchesRemovedVertex(finalBoundaryEdges[edgeIndex], removedVertices)
        )
            continue;
        if(foundTouchedComponent)
            return fail();

        foundTouchedComponent = true;
        visitedEdges[edgeIndex] = 1u;
        pendingEdges.clear();
        pendingEdges.push_back(edgeIndex);

        for(usize pendingIndex = 0u; pendingIndex < pendingEdges.size(); ++pendingIndex){
            const usize currentEdgeIndex = pendingEdges[pendingIndex];
            if(currentEdgeIndex >= finalBoundaryEdges.size())
                return fail();

            const EdgeRecord& currentEdge = finalBoundaryEdges[currentEdgeIndex];
            outBoundaryEdges.push_back(currentEdge);
            foundExistingCsgWallBoundary = foundExistingCsgWallBoundary
                || EdgeLooksLikeExistingCsgWallBoundary(currentEdge, restVertices, frameNormal)
            ;
            const u32 edgeVertices[2] = { currentEdge.a, currentEdge.b };
            for(const u32 vertex : edgeVertices){
                const auto foundFirst = firstEdgeByVertex.find(vertex);
                if(foundFirst == firstEdgeByVertex.end())
                    return fail();

                for(u32 adjacencyIndex = foundFirst.value(); adjacencyIndex != Limit<u32>::s_Max;){
                    if(adjacencyIndex >= edgeAdjacency.size())
                        return fail();

                    const EdgeAdjacencyEntry& adjacency = edgeAdjacency[adjacencyIndex];
                    adjacencyIndex = adjacency.next;
                    if(
                        adjacency.edgeIndex >= finalBoundaryEdges.size()
                        || visitedEdges[adjacency.edgeIndex] != 0u
                    )
                        continue;

                    visitedEdges[adjacency.edgeIndex] = 1u;
                    pendingEdges.push_back(adjacency.edgeIndex);
                }
            }
        }
    }
    if(!foundTouchedComponent || !foundExistingCsgWallBoundary || outBoundaryEdges.empty())
        return fail();

    if(outRemovedTriangleCount)
        *outRemovedTriangleCount = removedTriangleCount;
    return true;
}

[[nodiscard]] bool BuildHoleBoundaryEdgesFromRemovedTriangles(
    const Vector<u32>& indices,
    const Vector<DeformableVertexRest>& restVertices,
    const SIMDVector frameNormal,
    const Vector<u8, Core::Alloc::ScratchAllocator<u8>>& removedTriangles,
    Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>>& outBoundaryEdges,
    u32* outRemovedTriangleCount,
    Core::Alloc::ScratchArena<>& scratchArena
){
    if(
        Core::Geometry::BuildBoundaryEdgesFromRemovedTriangles(
            indices,
            removedTriangles,
            outBoundaryEdges,
            outRemovedTriangleCount
        )
    )
        return true;

    return BuildOpenBoundaryComponentFromRemovedTriangles(
        indices,
        restVertices,
        frameNormal,
        removedTriangles,
        outBoundaryEdges,
        outRemovedTriangleCount,
        scratchArena
    );
}

[[nodiscard]] bool BuildWholeTriangleHolePreviewBoundaryPlan(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    HoleFrame& outFrame,
    Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>>& outOrderedBoundaryEdges,
    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& outRestPositions,
    u32& outRemovedTriangleCount,
    Core::Alloc::ScratchArena<>& scratchArena
){
    outFrame = HoleFrame{};
    outOrderedBoundaryEdges.clear();
    outRestPositions.clear();
    outRemovedTriangleCount = 0u;

    const usize triangleCount = instance.indices.size() / 3u;
    u32 hitTriangleIndices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, params.posedHit.triangle, hitTriangleIndices))
        return false;

    f32 hitBary[3] = {};
    if(!DeformableValidation::NormalizeSourceBarycentric(params.posedHit.bary.values, hitBary))
        return false;
    if(!BuildHoleFrame(instance, hitTriangleIndices, hitBary, outFrame))
        return false;

    Vector<u8, Core::Alloc::ScratchAllocator<u8>> candidateTriangle{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    candidateTriangle.resize(triangleCount, 0u);
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> removeTriangle{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    removeTriangle.resize(triangleCount, 0u);
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        const usize indexBase = triangle * 3u;
        const u32 indices[3] = {
            instance.indices[indexBase + 0u],
            instance.indices[indexBase + 1u],
            instance.indices[indexBase + 2u],
        };

        const bool selectedTriangle = triangle == static_cast<usize>(params.posedHit.triangle);
        if(
            selectedTriangle
            || TriangleCentroidInsideOperatorVolume(instance, outFrame, params, indices)
        ){
            candidateTriangle[triangle] = 1u;
        }
    }

    if(
        !KeepConnectedCandidateTriangles(
            instance.indices,
            params.posedHit.triangle,
            candidateTriangle,
            removeTriangle,
            scratchArena
        )
    )
        return false;

    Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>> boundaryEdges{
        Core::Alloc::ScratchAllocator<EdgeRecord>(scratchArena)
    };
    if(
        !BuildHoleBoundaryEdgesFromRemovedTriangles(
            instance.indices,
            instance.restVertices,
            outFrame.normal,
            removeTriangle,
            boundaryEdges,
            &outRemovedTriangleCount,
            scratchArena
        )
    )
        return false;

    outRestPositions.reserve(instance.restVertices.size());
    for(const DeformableVertexRest& vertex : instance.restVertices)
        outRestPositions.push_back(vertex.position);

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

    const bool addWall = params.depth > DeformableRuntime::s_Epsilon;
    if(addWall && !BoundaryKeptFacesSupportHoleExtrusion(instance, outFrame.normal, removeTriangle, outOrderedBoundaryEdges))
        return false;

    return outOrderedBoundaryEdges.size() >= s_MinWallLoopVertexCount;
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
    if(UseOperatorSurfaceRemesh(params)){
        Vector<SurfaceRemeshTriangle, Core::Alloc::ScratchAllocator<SurfaceRemeshTriangle>> surfaceTriangles{
            Core::Alloc::ScratchAllocator<SurfaceRemeshTriangle>(scratchArena)
        };
        Vector<SurfaceRemeshGeneratedVertex, Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>> generatedVertices{
            Core::Alloc::ScratchAllocator<SurfaceRemeshGeneratedVertex>(scratchArena)
        };
        Vector<u8, Core::Alloc::ScratchAllocator<u8>> affectedTriangles{
            Core::Alloc::ScratchAllocator<u8>(scratchArena)
        };
        return BuildOperatorSurfaceRemeshPlan(
            instance,
            params,
            outFrame,
            outOrderedBoundaryEdges,
            outRestPositions,
            surfaceTriangles,
            generatedVertices,
            affectedTriangles,
            outRemovedTriangleCount,
            scratchArena
        );
    }

    return BuildWholeTriangleHolePreviewBoundaryPlan(
        instance,
        params,
        outFrame,
        outOrderedBoundaryEdges,
        outRestPositions,
        outRemovedTriangleCount,
        scratchArena
    );
}

[[nodiscard]] bool AppendHolePreviewMeshVertex(
    DeformableHolePreviewMesh& mesh,
    const Float3U& position,
    const Float3U& normal,
    const Float4U& color,
    u32& outVertexIndex
){
    outVertexIndex = Limit<u32>::s_Max;
    if(mesh.vertices.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return false;

    DeformableHolePreviewMeshVertex vertex;
    vertex.position = position;
    vertex.normal = normal;
    vertex.color = color;
    outVertexIndex = static_cast<u32>(mesh.vertices.size());
    mesh.vertices.push_back(vertex);
    return true;
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

    Vector<DeformableVertexRest, Core::Alloc::ScratchAllocator<DeformableVertexRest>> capRestVertices{
        Core::Alloc::ScratchAllocator<DeformableVertexRest>(scratchArena)
    };
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> capVertices{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
    capRestVertices.reserve(positions.size());
    capVertices.reserve(positions.size());
    for(usize i = 0u; i < positions.size(); ++i){
        DeformableVertexRest vertex;
        vertex.position = positions[i];
        capRestVertices.push_back(vertex);
        capVertices.push_back(static_cast<u32>(i));
    }

    Vector<u32, Core::Alloc::ScratchAllocator<u32>> capIndices{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
    if(!AppendBottomCapTriangles(capVertices, capRestVertices, tangent, bitangent, capIndices, nullptr))
        return false;

    const u32 vertexBase = static_cast<u32>(mesh.vertices.size());
    Float3U capNormal;
    StoreFloat(normal, &capNormal);
    for(const Float3U& position : positions){
        u32 ignoredVertex = Limit<u32>::s_Max;
        if(!AppendHolePreviewMeshVertex(mesh, position, capNormal, s_HolePreviewColor, ignoredVertex))
            return false;
    }

    if(capIndices.size() > static_cast<usize>(Limit<u32>::s_Max) - mesh.indices.size())
        return false;
    mesh.indices.reserve(mesh.indices.size() + capIndices.size());
    for(const u32 capIndex : capIndices)
        mesh.indices.push_back(vertexBase + capIndex);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





bool BeginSurfaceEdit(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePosedHit& hit,
    DeformableSurfaceEditSession& outSession
){
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
    DeformableHolePreview& outPreview
){
    outPreview = DeformableHolePreview{};
    session.previewParams = DeformableHoleEditParams{};
    session.previewed = false;
    if(
        !__hidden_deformable_surface_edit::ValidateUploadedRuntimePayload(instance)
        || !__hidden_deformable_surface_edit::ValidateSurfaceEditSessionParams(instance, session, params)
    )
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    __hidden_deformable_surface_edit::HoleFrame frame;
    Vector<
        __hidden_deformable_surface_edit::EdgeRecord,
        Core::Alloc::ScratchAllocator<__hidden_deformable_surface_edit::EdgeRecord>
    > orderedBoundaryEdges{
        Core::Alloc::ScratchAllocator<__hidden_deformable_surface_edit::EdgeRecord>(scratchArena)
    };
    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>> restPositions{
        Core::Alloc::ScratchAllocator<Float3U>(scratchArena)
    };
    u32 removedTriangleCount = 0u;
    if(
        !__hidden_deformable_surface_edit::BuildHolePreviewBoundaryPlan(
            instance,
            params,
            frame,
            orderedBoundaryEdges,
            restPositions,
            removedTriangleCount,
            scratchArena
        )
    )
        return false;

    if(__hidden_deformable_surface_edit::UseOperatorSurfaceRemesh(params)){
        DeformableHolePreviewMesh previewMesh;
        if(!BuildHolePreviewMesh(instance, params, previewMesh))
            return false;
    }

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

bool BuildOperatorFootprintFromGeometry(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorFootprint& outFootprint
){
    Core::Alloc::ScratchArena<> scratchArena;
    return __hidden_deformable_surface_edit::BuildOperatorFootprintFromGeometryImpl(
        vertices,
        outFootprint,
        scratchArena
    );
}

bool BuildOperatorProfileFromGeometry(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorProfile& outProfile
){
    Core::Alloc::ScratchArena<> scratchArena;
    return __hidden_deformable_surface_edit::BuildOperatorProfileFromGeometryImpl(
        vertices,
        outProfile,
        scratchArena
    );
}

bool BuildOperatorShapeFromGeometry(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorFootprint& outFootprint,
    DeformableOperatorProfile& outProfile
){
    outFootprint = DeformableOperatorFootprint{};
    outProfile = DeformableOperatorProfile{};
    Core::Alloc::ScratchArena<> scratchArena;
    return
        __hidden_deformable_surface_edit::BuildOperatorFootprintFromGeometryImpl(
            vertices,
            outFootprint,
            scratchArena
        )
        && __hidden_deformable_surface_edit::BuildOperatorProfileFromGeometryImpl(
            vertices,
            outProfile,
            scratchArena
        )
    ;
}

bool BuildHolePreviewMesh(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    DeformableHolePreviewMesh& outMesh
){
    outMesh = DeformableHolePreviewMesh{};
    if(
        !__hidden_deformable_surface_edit::ValidateUploadedRuntimePayload(instance)
        || !__hidden_deformable_surface_edit::ValidateParams(instance, params)
    )
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    __hidden_deformable_surface_edit::HoleFrame frame;
    Vector<
        __hidden_deformable_surface_edit::EdgeRecord,
        Core::Alloc::ScratchAllocator<__hidden_deformable_surface_edit::EdgeRecord>
    > orderedBoundaryEdges{
        Core::Alloc::ScratchAllocator<__hidden_deformable_surface_edit::EdgeRecord>(scratchArena)
    };
    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>> restPositions{
        Core::Alloc::ScratchAllocator<Float3U>(scratchArena)
    };
    u32 removedTriangleCount = 0u;
    if(
        !__hidden_deformable_surface_edit::BuildHolePreviewBoundaryPlan(
            instance,
            params,
            frame,
            orderedBoundaryEdges,
            restPositions,
            removedTriangleCount,
            scratchArena
        )
    )
        return false;

    const usize boundaryVertexCount = orderedBoundaryEdges.size();
    if(
        boundaryVertexCount < __hidden_deformable_surface_edit::s_MinWallLoopVertexCount
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
        __hidden_deformable_surface_edit::s_HolePreviewSurfaceOffsetMin,
        params.radius * __hidden_deformable_surface_edit::s_HolePreviewSurfaceOffsetRadiusScale
    );
    for(const __hidden_deformable_surface_edit::EdgeRecord& edge : orderedBoundaryEdges){
        if(edge.a >= restPositions.size())
            return false;

        Float3U topPosition;
        StoreFloat(
            VectorMultiplyAdd(
                frame.normal,
                VectorReplicate(surfaceOffset),
                LoadFloat(restPositions[edge.a])
            ),
            &topPosition
        );
        topCapPositions.push_back(topPosition);
    }

    if(
        !__hidden_deformable_surface_edit::AppendHolePreviewCap(
            topCapPositions,
            frame.normal,
            frame.tangent,
            frame.bitangent,
            outMesh,
            scratchArena
        )
    )
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
            !__hidden_deformable_surface_edit::ApplyOperatorProfileToWallVertexPlan(
                orderedBoundaryEdges,
                restPositions,
                frame,
                params,
                wallVertexPlan.data(),
                wallVertexPlan.size()
            )
        )
            return false;

        Vector<u32, Core::Alloc::ScratchAllocator<u32>> sideTopVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        Vector<u32, Core::Alloc::ScratchAllocator<u32>> sideBottomVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>> bottomCapPositions{
            Core::Alloc::ScratchAllocator<Float3U>(scratchArena)
        };
        sideTopVertices.reserve(boundaryVertexCount);
        sideBottomVertices.reserve(boundaryVertexCount);
        bottomCapPositions.reserve(boundaryVertexCount);
        for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
            u32 topVertex = Limit<u32>::s_Max;
            u32 bottomVertex = Limit<u32>::s_Max;
            if(
                !__hidden_deformable_surface_edit::AppendHolePreviewMeshVertex(
                    outMesh,
                    topCapPositions[edgeIndex],
                    wallVertexPlan[edgeIndex].normal,
                    __hidden_deformable_surface_edit::s_HolePreviewColor,
                    topVertex
                )
                || !__hidden_deformable_surface_edit::AppendHolePreviewMeshVertex(
                    outMesh,
                    wallVertexPlan[edgeIndex].position,
                    wallVertexPlan[edgeIndex].normal,
                    __hidden_deformable_surface_edit::s_HolePreviewColor,
                    bottomVertex
                )
            )
                return false;

            sideTopVertices.push_back(topVertex);
            sideBottomVertices.push_back(bottomVertex);
            bottomCapPositions.push_back(wallVertexPlan[edgeIndex].position);
        }

        if(boundaryVertexCount > (static_cast<usize>(Limit<u32>::s_Max) - outMesh.indices.size()) / 6u)
            return false;

        outMesh.indices.reserve(outMesh.indices.size() + boundaryVertexCount * 6u);
        for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
            const usize nextEdgeIndex = (edgeIndex + 1u) % boundaryVertexCount;
            outMesh.indices.push_back(sideTopVertices[edgeIndex]);
            outMesh.indices.push_back(sideTopVertices[nextEdgeIndex]);
            outMesh.indices.push_back(sideBottomVertices[nextEdgeIndex]);
            outMesh.indices.push_back(sideTopVertices[edgeIndex]);
            outMesh.indices.push_back(sideBottomVertices[nextEdgeIndex]);
            outMesh.indices.push_back(sideBottomVertices[edgeIndex]);
        }

        if(
            !__hidden_deformable_surface_edit::AppendHolePreviewCap(
                bottomCapPositions,
                frame.normal,
                frame.tangent,
                frame.bitangent,
                outMesh,
                scratchArena
            )
        )
            return false;
    }

    outMesh.removedTriangleCount = removedTriangleCount;
    outMesh.wallVertexCount = static_cast<u32>(boundaryVertexCount);
    outMesh.valid = !outMesh.vertices.empty() && !outMesh.indices.empty();
    return outMesh.valid;
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

    const bool validRuntimePayload = __hidden_deformable_surface_edit::ValidateUploadedRuntimePayload(instance);
    if(!validRuntimePayload){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: commit failed before edit, runtime payload is invalid or not uploaded (entity={} runtime_mesh={} dirty_flags={} vertices={} triangles={})")
            , instance.entity.id
            , instance.handle.value
            , static_cast<u32>(instance.dirtyFlags)
            , instance.restVertices.size()
            , instance.indices.size() / 3u
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
    if(!CommitDeformableRestSpaceHole(instance, params, &result)){
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
        SIMDVector normal = Core::Geometry::FrameNormalizeDirection(
            VectorSubtract(rimPosition, innerPosition),
            VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
        );
        if(
            !__hidden_deformable_surface_edit::FiniteVec3(normal)
            || VectorGetX(Vector3LengthSq(normal)) <= Core::Geometry::s_FrameDirectionEpsilon
        )
            return false;

        const SIMDVector accessoryPosition = VectorMultiplyAdd(
            normal,
            VectorReplicate(attachment.normalOffset()),
            rimPosition
        );
        if(!__hidden_deformable_surface_edit::FiniteVec3(accessoryPosition))
            return false;

        const SIMDVector tangent = Core::Geometry::FrameResolveTangent(
            normal,
            VectorSubtract(rimB, rimA),
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
    SIMDVector normal = Core::Geometry::FrameNormalizeDirection(
        VectorSubtract(rimCenter, innerCenter),
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
    );
    if(
        !__hidden_deformable_surface_edit::FiniteVec3(normal)
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
    for(const DeformableSurfaceEditRecord& record : state.edits)
        AppendPOD(outBinary, record);
    for(const __hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinary& binaryRecord : accessoryRecords)
        AppendPOD(outBinary, binaryRecord);
    outBinary.insert(outBinary.end(), stringTable.begin(), stringTable.end());
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
    outState.edits.reserve(editRecordCount);
    for(usize i = 0u; i < editRecordCount; ++i){
        DeformableSurfaceEditRecord record;
        if(!ReadPOD(binary, cursor, record) || !__hidden_deformable_surface_edit::ValidEditRecord(record)){
            outState = DeformableSurfaceEditState{};
            return false;
        }
        outState.edits.push_back(record);
    }

    const usize accessoryRecordCount = static_cast<usize>(accessoryCount);
    using AccessoryRecord = __hidden_deformable_surface_edit::SurfaceEditAccessoryRecordBinary;
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<AccessoryRecord, Core::Alloc::ScratchAllocator<AccessoryRecord>> accessoryRecords{
        Core::Alloc::ScratchAllocator<AccessoryRecord>(scratchArena)
    };
    accessoryRecords.reserve(accessoryRecordCount);
    for(usize i = 0u; i < accessoryRecordCount; ++i){
        AccessoryRecord binaryRecord;
        if(!ReadPOD(binary, cursor, binaryRecord)){
            outState = DeformableSurfaceEditState{};
            return false;
        }
        accessoryRecords.push_back(binaryRecord);
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
            "edit[{}] id={} type=hole base_revision={} result_revision={} source_tri={} bary=({},{},{}) rest_position=({},{},{}) rest_normal=({},{},{}) radius={} ellipse={} depth={} operator_footprint_vertices={} operator_profile_samples={} loop_cuts={} wall_span=({},{}) removed_triangles={} added_vertices={} added_triangles={}\n",
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
    SIMDVector rawUv = VectorZero();
    for(u32 i = 0u; i < 3u; ++i){
        const u32 sourceVertex = generated.sourceVertices[i];
        const f32 weight = generated.bary[i];
        if(sourceVertex >= instance.restVertices.size() || !IsFinite(weight) || weight < 0.0f)
            return false;

        const DeformableVertexRest& source = instance.restVertices[sourceVertex];
        const SIMDVector weightVector = VectorReplicate(weight);
        rawNormal = VectorMultiplyAdd(LoadRestVertexNormal(source), weightVector, rawNormal);
        rawTangent = VectorMultiplyAdd(VectorSetW(LoadRestVertexTangent(source), 0.0f), weightVector, rawTangent);
        rawUv = VectorMultiplyAdd(LoadRestVertexUv0(source), weightVector, rawUv);
    }

    const SIMDVector normal = Core::Geometry::FrameNormalizeDirection(rawNormal, fallbackNormal);
    SIMDVector tangent;
    SIMDVector bitangent;
    ResolveTangentBitangentVectors(normal, rawTangent, fallbackTangent, tangent, bitangent);
    if(!FiniteVec3(normal) || !FiniteVec3(tangent))
        return false;

    StoreFloat(normal, &vertex.normal);
    StoreFloat(VectorSetW(tangent, 1.0f), &vertex.tangent);
    StoreFloat(rawUv, &vertex.uv0);
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

[[nodiscard]] bool CommitDeformableRestSpaceHoleRemeshedImpl(
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
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> affectedTriangles{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
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
            affectedTriangles,
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
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        if(affectedTriangles[triangle] == 0u)
            continue;

        const DeformableEditMaskFlags editMaskFlags = resolveValidatedEditMask(triangle);
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
    newRestVertices.assign(instance.restVertices.begin(), instance.restVertices.end());
    if(!instance.skin.empty()){
        newSkin.reserve(reservedVertexCount);
        newSkin.assign(instance.skin.begin(), instance.skin.end());
    }
    if(!instance.sourceSamples.empty()){
        newSourceSamples.reserve(reservedVertexCount);
        newSourceSamples.assign(instance.sourceSamples.begin(), instance.sourceSamples.end());
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
    for(const SurfaceRemeshTriangle& triangle : surfaceTriangles){
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

        Vector<u32, Core::Alloc::ScratchAllocator<u32>> wallVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        wallVertices.resize(totalWallVertexCount, 0u);

        auto appendPlannedVertex = [&](
            const Core::Geometry::SurfacePatchWallVertex& plannedVertex,
            const SIMDVector normal,
            const SIMDVector tangent,
            const Float2U& uv0,
            u32& outVertex){
            if(!newSourceSamples.empty())
                newSourceSamples[plannedVertex.sourceVertex] = wallSourceSample;

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
            const usize wallVertexBase = ringIndex * boundaryVertexCount;
            for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
                const Core::Geometry::SurfacePatchWallVertex& plannedVertex = wallVertexPlan[wallVertexBase + edgeIndex];
                if(
                    !appendPlannedVertex(
                        plannedVertex,
                        LoadFloat(plannedVertex.normal),
                        LoadFloat(plannedVertex.tangent),
                        plannedVertex.uv0,
                        wallVertices[wallVertexBase + edgeIndex]
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
            }
        }

        Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>> bandOuterEdges = orderedBoundaryEdges;
        Vector<u32, Core::Alloc::ScratchAllocator<u32>> ringVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        ringVertices.reserve(boundaryVertexCount);
        for(usize ringIndex = 0u; ringIndex < wallBandCount; ++ringIndex){
            const usize wallVertexBase = ringIndex * boundaryVertexCount;
            ringVertices.clear();
            for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex)
                ringVertices.push_back(wallVertices[wallVertexBase + edgeIndex]);

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
                        wallVertices.data() + wallVertexBase,
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

        Vector<u32, Core::Alloc::ScratchAllocator<u32>> capVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        capVertices.reserve(boundaryVertexCount);
        const usize capSourceVertexBase = (wallBandCount - 1u) * boundaryVertexCount;
        for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex)
            capVertices.push_back(wallVertices[capSourceVertexBase + edgeIndex]);

        u32 capAddedTriangleCount = 0u;
        if(
            !AppendBottomCapTriangles(
                capVertices,
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
                , capVertices.size()
            );
            return false;
        }
        if(hasEditMaskPerTriangle){
            for(u32 triangleOffset = 0u; triangleOffset < capAddedTriangleCount; ++triangleOffset)
                newEditMaskPerTriangle.push_back(removedEditMaskFlags);
        }
        addedTriangleCount += capAddedTriangleCount;
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

    Vector<DeformableVertexRest> rebuiltRestVertices = newRestVertices;
    if(DeformableValidation::RebuildRestVertexTangentFrames(rebuiltRestVertices, newIndices))
        newRestVertices = Move(rebuiltRestVertices);

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


[[nodiscard]] bool CommitDeformableRestSpaceHoleImpl(
    DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    const bool requireUploadedRuntimePayload,
    const u32 wallLoopCutCount,
    DeformableHoleEditResult* outResult
){
    if(outResult)
        *outResult = DeformableHoleEditResult{};
    if(UseOperatorSurfaceRemesh(params)){
        return CommitDeformableRestSpaceHoleRemeshedImpl(
            instance,
            params,
            requireUploadedRuntimePayload,
            wallLoopCutCount,
            outResult
        );
    }

    const bool validPayload = requireUploadedRuntimePayload
        ? ValidateUploadedRuntimePayload(instance)
        : ValidateRuntimePayload(instance)
    ;
    const bool validParams = ValidateParams(instance, params);
    const bool validWallLoopCutCount = ValidWallLoopCutCount(wallLoopCutCount);
    const bool validWallLoopDepth = wallLoopCutCount == 0u || params.depth > DeformableRuntime::s_Epsilon;
    if(!validPayload || !validParams || !validWallLoopCutCount || !validWallLoopDepth){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: rest-space hole validation failed (entity={} runtime_mesh={} uploaded_required={} valid_payload={} valid_params={} valid_loop_cuts={} valid_loop_depth={} dirty_flags={} revision={} triangle={})")
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

    const bool hasEditMaskPerTriangle = !instance.editMaskPerTriangle.empty();
    auto resolveValidatedEditMask = [&instance, hasEditMaskPerTriangle](const usize triangle){
        return
            hasEditMaskPerTriangle
                ? instance.editMaskPerTriangle[triangle]
                : s_DeformableEditMaskDefault
        ;
    };

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> candidateTriangle{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    candidateTriangle.resize(triangleCount, 0u);
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> removeTriangle{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    removeTriangle.resize(triangleCount, 0u);
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        const usize indexBase = triangle * 3u;
        const u32 indices[3] = {
            instance.indices[indexBase + 0u],
            instance.indices[indexBase + 1u],
            instance.indices[indexBase + 2u],
        };

        const bool selectedTriangle = triangle == static_cast<usize>(params.posedHit.triangle);
        if(
            selectedTriangle
            || TriangleCentroidInsideOperatorVolume(instance, frame, params, indices)
        ){
            candidateTriangle[triangle] = 1u;
        }
    }

    if(
        !KeepConnectedCandidateTriangles(
            instance.indices,
            params.posedHit.triangle,
            candidateTriangle,
            removeTriangle,
            scratchArena
        )
    )
        return false;

    DeformableEditMaskFlags removedEditMaskFlags = 0u;
    u32 selectedTriangleCount = 0u;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        if(removeTriangle[triangle] == 0u)
            continue;

        const DeformableEditMaskFlags editMaskFlags = resolveValidatedEditMask(triangle);
        if(!DeformableEditMaskAllowsCommit(editMaskFlags))
            return false;

        removedEditMaskFlags = static_cast<DeformableEditMaskFlags>(removedEditMaskFlags | editMaskFlags);
        ++selectedTriangleCount;
    }

    if(removedEditMaskFlags == 0u)
        removedEditMaskFlags = s_DeformableEditMaskDefault;

    using EdgeRecordVector = Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>>;
    EdgeRecordVector boundaryEdges{
        Core::Alloc::ScratchAllocator<EdgeRecord>(scratchArena)
    };
    u32 removedTriangleCount = 0u;
    if(
        !BuildHoleBoundaryEdgesFromRemovedTriangles(
            instance.indices,
            instance.restVertices,
            frame.normal,
            removeTriangle,
            boundaryEdges,
            &removedTriangleCount,
            scratchArena
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: rest-space hole boundary build failed (entity={} runtime_mesh={} hit_triangle={} triangles={} selected_triangles={} radius={} ellipse={})")
            , instance.entity.id
            , instance.handle.value
            , params.posedHit.triangle
            , triangleCount
            , selectedTriangleCount
            , params.radius
            , params.ellipseRatio
        );
        return false;
    }

    Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>> restPositions{
        Core::Alloc::ScratchAllocator<Float3U>(scratchArena)
    };
    restPositions.reserve(instance.restVertices.size());
    for(const DeformableVertexRest& vertex : instance.restVertices)
        restPositions.push_back(vertex.position);

    Core::Geometry::MeshTopologyBoundaryLoopFrame topologyFrame;
    StoreFloat(frame.center, &topologyFrame.center);
    StoreFloat(frame.tangent, &topologyFrame.tangent);
    StoreFloat(frame.bitangent, &topologyFrame.bitangent);

    EdgeRecordVector orderedBoundaryEdges{
        Core::Alloc::ScratchAllocator<EdgeRecord>(scratchArena)
    };
    if(
        !Core::Geometry::BuildOrderedBoundaryLoop(
            boundaryEdges,
            restPositions,
            topologyFrame,
            orderedBoundaryEdges
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: rest-space hole ordered boundary loop failed (entity={} runtime_mesh={} hit_triangle={} boundary_edges={} removed_triangles={})")
            , instance.entity.id
            , instance.handle.value
            , params.posedHit.triangle
            , boundaryEdges.size()
            , removedTriangleCount
        );
        return false;
    }

    const bool addWall = params.depth > DeformableRuntime::s_Epsilon;
    if(addWall && !BoundaryKeptFacesSupportHoleExtrusion(instance, frame.normal, removeTriangle, orderedBoundaryEdges)){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: rest-space hole boundary intersects a hard kept face (entity={} runtime_mesh={} hit_triangle={} boundary_edges={} removed_triangles={} depth={})")
            , instance.entity.id
            , instance.handle.value
            , params.posedHit.triangle
            , orderedBoundaryEdges.size()
            , removedTriangleCount
            , params.depth
        );
        return false;
    }

    Vector<u32> newIndices;
    u32 newSourceTriangleCount = instance.sourceTriangleCount;
    if(instance.sourceSamples.empty() || instance.sourceSamples.size() != instance.restVertices.size() || newSourceTriangleCount == 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("DeformableSurfaceEdit: rest-space hole source samples are invalid (entity={} runtime_mesh={} vertices={} source_samples={} source_triangles={})")
            , instance.entity.id
            , instance.handle.value
            , instance.restVertices.size()
            , instance.sourceSamples.size()
            , newSourceTriangleCount
        );
        return false;
    }

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
        if(totalWallVertexCount > Limit<usize>::s_Max - instance.restVertices.size())
            return false;
        if(instance.restVertices.size() + totalWallVertexCount > static_cast<usize>(Limit<u32>::s_Max))
            return false;
        capTriangleCount = wallVertexCount - 2u;
    }

    const usize removedIndexCount = static_cast<usize>(removedTriangleCount) * 3u;
    const usize wallIndexCount = addWall
        ? wallVertexCount * 6u * wallBandCount
        : 0u
    ;
    const usize capIndexCount = capTriangleCount * 3u;
    const usize keptIndexCount = instance.indices.size() - removedIndexCount;
    if(
        wallIndexCount > Limit<usize>::s_Max - capIndexCount
        || wallIndexCount + capIndexCount > Limit<usize>::s_Max - keptIndexCount
        || keptIndexCount + wallIndexCount + capIndexCount > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    usize reservedVertexCount = instance.restVertices.size();
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
    newRestVertices.assign(instance.restVertices.begin(), instance.restVertices.end());
    if(hasEditMaskPerTriangle)
        newEditMaskPerTriangle.reserve((keptIndexCount + wallIndexCount + capIndexCount) / 3u);
    if(addWall){
        if(!instance.skin.empty()){
            newSkin.reserve(reservedVertexCount);
            newSkin.assign(instance.skin.begin(), instance.skin.end());
        }
        if(!instance.sourceSamples.empty()){
            newSourceSamples.reserve(reservedVertexCount);
            newSourceSamples.assign(instance.sourceSamples.begin(), instance.sourceSamples.end());
        }
        newMorphs = instance.morphs;
    }

    newIndices.reserve(keptIndexCount + wallIndexCount + capIndexCount);
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
            newEditMaskPerTriangle.push_back(resolveValidatedEditMask(triangle));
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

        Vector<u32, Core::Alloc::ScratchAllocator<u32>> wallVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        wallVertices.resize(totalWallVertexCount, 0u);

        auto appendPlannedVertex = [&](
            const Core::Geometry::SurfacePatchWallVertex& plannedVertex,
            const SIMDVector normal,
            const SIMDVector tangent,
            const Float2U& uv0,
            u32& outVertex){
            if(!newSourceSamples.empty())
                newSourceSamples[plannedVertex.sourceVertex] = wallSourceSample;

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
            const usize wallVertexBase = ringIndex * boundaryVertexCount;

            for(usize edgeIndex = 0; edgeIndex < boundaryVertexCount; ++edgeIndex){
                const Core::Geometry::SurfacePatchWallVertex& plannedVertex = wallVertexPlan[wallVertexBase + edgeIndex];
                if(
                    !appendPlannedVertex(
                        plannedVertex,
                        LoadFloat(plannedVertex.normal),
                        LoadFloat(plannedVertex.tangent),
                        plannedVertex.uv0,
                        wallVertices[wallVertexBase + edgeIndex]
                    )
                )
                    return false;
            }
        }

        EdgeRecordVector bandOuterEdges = orderedBoundaryEdges;
        Vector<u32, Core::Alloc::ScratchAllocator<u32>> ringVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        ringVertices.reserve(boundaryVertexCount);
        for(usize ringIndex = 0u; ringIndex < wallBandCount; ++ringIndex){
            const usize wallVertexBase = ringIndex * boundaryVertexCount;
            ringVertices.clear();
            for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex)
                ringVertices.push_back(wallVertices[wallVertexBase + edgeIndex]);

            if(
                !TransferWallMorphDeltas(
                    newMorphs,
                    orderedBoundaryEdges,
                    ringVertices
                )
            )
                return false;

            u32 wallAddedTriangleCount = 0u;
            if(
                !Core::Geometry::AppendWallTrianglePairs(
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
                if(
                    !Core::Geometry::BuildSurfacePatchRingEdges(
                        wallVertices.data() + wallVertexBase,
                        boundaryVertexCount,
                        bandOuterEdges
                    )
                )
                    return false;
            }
        }

        Vector<u32, Core::Alloc::ScratchAllocator<u32>> capVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        capVertices.reserve(boundaryVertexCount);
        const usize capSourceVertexBase = (wallBandCount - 1u) * boundaryVertexCount;
        for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex)
            capVertices.push_back(wallVertices[capSourceVertexBase + edgeIndex]);

        u32 capAddedTriangleCount = 0u;
        if(
            !AppendBottomCapTriangles(
                capVertices,
                newRestVertices,
                frame.tangent,
                frame.bitangent,
                newIndices,
                &capAddedTriangleCount
            )
        )
            return false;
        if(hasEditMaskPerTriangle){
            for(u32 triangleOffset = 0u; triangleOffset < capAddedTriangleCount; ++triangleOffset)
                newEditMaskPerTriangle.push_back(removedEditMaskFlags);
        }
        addedTriangleCount += capAddedTriangleCount;
    }

    if(!DeformableValidation::RebuildRestVertexTangentFrames(newRestVertices, newIndices))
        return false;

    if(
        !DeformableValidation::ValidRuntimePayloadArrays(
            newRestVertices,
            newIndices,
            newSourceTriangleCount,
            instance.skeletonJointCount,
            addWall ? newSkin : instance.skin,
            instance.inverseBindMatrices,
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
            !CommitDeformableRestSpaceHoleImpl(
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
    DeformableHoleEditResult* outResult
){
    return __hidden_deformable_surface_edit::CommitDeformableRestSpaceHoleImpl(instance, params, true, 0u, outResult);
}


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

