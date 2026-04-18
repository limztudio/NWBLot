// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_surface_edit.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_surface_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_Epsilon = 0.000001f;
static constexpr f32 s_FrameEpsilon = 0.00000001f;

struct Vec3{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
};

struct HoleFrame{
    Vec3 center;
    Vec3 normal;
    Vec3 tangent;
    Vec3 bitangent;
};

struct EdgeRecord{
    u32 a = 0;
    u32 b = 0;
    u32 fullCount = 0;
    u32 removedCount = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] f32 AbsF32(const f32 value){
    return value < 0.0f ? -value : value;
}

[[nodiscard]] bool ActiveLength(const f32 value){
    return value > s_Epsilon;
}

[[nodiscard]] bool ValidBarycentric(const f32 (&bary)[3]){
    const f32 barySum = bary[0] + bary[1] + bary[2];
    return IsFinite(bary[0])
        && IsFinite(bary[1])
        && IsFinite(bary[2])
        && bary[0] >= -s_Epsilon
        && bary[1] >= -s_Epsilon
        && bary[2] >= -s_Epsilon
        && AbsF32(barySum - 1.0f) <= 0.001f
    ;
}

[[nodiscard]] bool IsFiniteFloat2(const Float2Data& value){
    return IsFinite(value.x) && IsFinite(value.y);
}

[[nodiscard]] bool IsFiniteFloat3(const Float3Data& value){
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

[[nodiscard]] bool IsFiniteFloat4(const Float4Data& value){
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

[[nodiscard]] bool ValidRestVertex(const DeformableVertexRest& vertex){
    return IsFiniteFloat3(vertex.position)
        && IsFiniteFloat3(vertex.normal)
        && IsFiniteFloat4(vertex.tangent)
        && IsFiniteFloat2(vertex.uv0)
        && IsFiniteFloat4(vertex.color0)
    ;
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

[[nodiscard]] Vec3 Scale(const Vec3& value, const f32 scale){
    return Vec3{ value.x * scale, value.y * scale, value.z * scale };
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

[[nodiscard]] Vec3 BarycentricPoint(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&indices)[3],
    const f32 (&bary)[3])
{
    const Vec3 a = ToVec3(instance.restVertices[indices[0]].position);
    const Vec3 b = ToVec3(instance.restVertices[indices[1]].position);
    const Vec3 c = ToVec3(instance.restVertices[indices[2]].position);
    return Add(Add(Scale(a, bary[0]), Scale(b, bary[1])), Scale(c, bary[2]));
}

[[nodiscard]] Vec3 TriangleCentroid(const DeformableRuntimeMeshInstance& instance, const u32 (&indices)[3]){
    const Vec3 a = ToVec3(instance.restVertices[indices[0]].position);
    const Vec3 b = ToVec3(instance.restVertices[indices[1]].position);
    const Vec3 c = ToVec3(instance.restVertices[indices[2]].position);
    return Scale(Add(Add(a, b), c), 1.0f / 3.0f);
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

[[nodiscard]] u64 MakeEdgeKey(const u32 a, const u32 b){
    const u32 lo = a < b ? a : b;
    const u32 hi = a < b ? b : a;
    return (static_cast<u64>(lo) << 32u) | static_cast<u64>(hi);
}

using EdgeRecordMap = HashMap<u64, EdgeRecord, Hasher<u64>, EqualTo<u64>>;
using VertexDegreeMap = HashMap<u32, u32, Hasher<u32>, EqualTo<u32>>;
using InnerVertexMap = HashMap<u32, u32, Hasher<u32>, EqualTo<u32>>;

void RegisterFullEdge(EdgeRecordMap& edges, const u32 a, const u32 b){
    auto [it, inserted] = edges.emplace(MakeEdgeKey(a, b), EdgeRecord{});
    EdgeRecord& record = it.value();
    if(inserted){
        record.a = a;
        record.b = b;
    }
    ++record.fullCount;
}

[[nodiscard]] bool RegisterRemovedEdge(EdgeRecordMap& edges, const u32 a, const u32 b){
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

void IncrementVertexDegree(VertexDegreeMap& degrees, const u32 vertex){
    auto [it, inserted] = degrees.emplace(vertex, 0u);
    ++it.value();
}

[[nodiscard]] bool BuildHoleFrame(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&triangleIndices)[3],
    const f32 (&bary)[3],
    HoleFrame& outFrame)
{
    const Vec3 a = ToVec3(instance.restVertices[triangleIndices[0]].position);
    const Vec3 b = ToVec3(instance.restVertices[triangleIndices[1]].position);
    const Vec3 c = ToVec3(instance.restVertices[triangleIndices[2]].position);
    const Vec3 edge0 = Subtract(b, a);
    const Vec3 edge1 = Subtract(c, a);

    outFrame.center = BarycentricPoint(instance, triangleIndices, bary);
    outFrame.normal = Normalize(Cross(edge0, edge1), Vec3{ 0.0f, 0.0f, 1.0f });

    Vec3 tangent{
        instance.restVertices[triangleIndices[0]].tangent.x,
        instance.restVertices[triangleIndices[0]].tangent.y,
        instance.restVertices[triangleIndices[0]].tangent.z,
    };
    tangent = Subtract(tangent, Scale(outFrame.normal, Dot(tangent, outFrame.normal)));
    if(LengthSquared(tangent) <= s_FrameEpsilon)
        tangent = Subtract(edge0, Scale(outFrame.normal, Dot(edge0, outFrame.normal)));
    outFrame.tangent = Normalize(tangent, Vec3{ 1.0f, 0.0f, 0.0f });
    outFrame.bitangent = Normalize(Cross(outFrame.normal, outFrame.tangent), Vec3{ 0.0f, 1.0f, 0.0f });
    return LengthSquared(outFrame.normal) > s_FrameEpsilon
        && LengthSquared(outFrame.tangent) > s_FrameEpsilon
        && LengthSquared(outFrame.bitangent) > s_FrameEpsilon
    ;
}

[[nodiscard]] bool ValidateRuntimePayload(const DeformableRuntimeMeshInstance& instance){
    if(!instance.entity.valid() || !instance.handle.valid() || instance.restVertices.empty() || instance.indices.empty())
        return false;
    if(instance.restVertices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || instance.indices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || (instance.indices.size() % 3u) != 0u
    )
        return false;
    if(!instance.skin.empty() && instance.skin.size() != instance.restVertices.size())
        return false;
    if(!instance.sourceSamples.empty() && instance.sourceSamples.size() != instance.restVertices.size())
        return false;

    for(const DeformableVertexRest& vertex : instance.restVertices){
        if(!ValidRestVertex(vertex))
            return false;
    }
    for(const u32 index : instance.indices){
        if(index >= instance.restVertices.size())
            return false;
    }
    return true;
}

[[nodiscard]] bool ValidateParams(const DeformableRuntimeMeshInstance& instance, const DeformableHoleEditParams& params){
    if(!ValidBarycentric(params.posedHit.bary))
        return false;
    if(params.posedHit.entity.valid() && params.posedHit.entity != instance.entity)
        return false;
    if(params.posedHit.runtimeMesh.valid() && params.posedHit.runtimeMesh != instance.handle)
        return false;
    if(params.posedHit.editRevision != instance.editRevision)
        return false;
    return IsFinite(params.radius)
        && IsFinite(params.ellipseRatio)
        && IsFinite(params.depth)
        && IsFinite(params.radius * params.ellipseRatio)
        && ActiveLength(params.radius)
        && ActiveLength(params.radius * params.ellipseRatio)
        && params.depth >= 0.0f
        && instance.editRevision != Limit<u32>::s_Max
    ;
}

[[nodiscard]] bool BoundaryEdgesFormSingleLoop(const Vector<EdgeRecord>& boundaryEdges){
    if(boundaryEdges.empty())
        return false;

    Vector<u8> visitedEdges;
    visitedEdges.resize(boundaryEdges.size(), 0u);

    const u32 startVertex = boundaryEdges[0].a;
    u32 currentVertex = startVertex;
    usize visitedCount = 0;
    while(visitedCount < boundaryEdges.size()){
        usize nextEdgeIndex = Limit<usize>::s_Max;
        u32 nextVertex = 0;
        for(usize edgeIndex = 0; edgeIndex < boundaryEdges.size(); ++edgeIndex){
            if(visitedEdges[edgeIndex] != 0u)
                continue;

            const EdgeRecord& edge = boundaryEdges[edgeIndex];
            if(edge.a == currentVertex){
                nextEdgeIndex = edgeIndex;
                nextVertex = edge.b;
                break;
            }
            if(edge.b == currentVertex){
                nextEdgeIndex = edgeIndex;
                nextVertex = edge.a;
                break;
            }
        }

        if(nextEdgeIndex == Limit<usize>::s_Max)
            return false;

        visitedEdges[nextEdgeIndex] = 1u;
        ++visitedCount;
        currentVertex = nextVertex;
        if(currentVertex == startVertex)
            return visitedCount == boundaryEdges.size();
    }

    return false;
}

[[nodiscard]] bool TransferMorphDeltasForInnerVertex(
    Vector<DeformableMorph>& morphs,
    const u32 outerVertex,
    const u32 innerVertex)
{
    for(DeformableMorph& morph : morphs){
        const usize sourceDeltaCount = morph.deltas.size();
        for(usize deltaIndex = 0; deltaIndex < sourceDeltaCount; ++deltaIndex){
            if(morph.deltas[deltaIndex].vertexId != outerVertex)
                continue;
            if(morph.deltas.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;

            DeformableMorphDelta delta = morph.deltas[deltaIndex];
            delta.vertexId = innerVertex;
            morph.deltas.push_back(delta);
        }
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
    const Vec3 centroid = TriangleCentroid(instance, triangleIndices);
    const Vec3 offset = Subtract(centroid, frame.center);
    const f32 x = Dot(offset, frame.tangent) / radiusX;
    const f32 y = Dot(offset, frame.bitangent) / radiusY;
    return ((x * x) + (y * y)) <= 1.0f;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CommitDeformableRestSpaceHole(
    DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    DeformableHoleEditResult* outResult)
{
    if(outResult)
        *outResult = DeformableHoleEditResult{};
    if(!__hidden_deformable_surface_edit::ValidateRuntimePayload(instance)
        || !__hidden_deformable_surface_edit::ValidateParams(instance, params)
    )
        return false;

    const usize triangleCount = instance.indices.size() / 3u;
    u32 hitTriangleIndices[3] = {};
    if(!__hidden_deformable_surface_edit::ValidateTriangleIndex(instance, params.posedHit.triangle, hitTriangleIndices))
        return false;

    __hidden_deformable_surface_edit::HoleFrame frame;
    if(!__hidden_deformable_surface_edit::BuildHoleFrame(instance, hitTriangleIndices, params.posedHit.bary, frame))
        return false;

    const f32 radiusX = params.radius;
    const f32 radiusY = params.radius * params.ellipseRatio;
    Vector<u8> removeTriangle;
    removeTriangle.resize(triangleCount, 0u);

    u32 removedTriangleCount = 0;
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        u32 indices[3] = {};
        if(!__hidden_deformable_surface_edit::ValidateTriangleIndex(instance, static_cast<u32>(triangle), indices))
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

    __hidden_deformable_surface_edit::EdgeRecordMap edges;
    edges.reserve(instance.indices.size());
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        u32 indices[3] = {};
        if(!__hidden_deformable_surface_edit::ValidateTriangleIndex(instance, static_cast<u32>(triangle), indices))
            return false;

        __hidden_deformable_surface_edit::RegisterFullEdge(edges, indices[0], indices[1]);
        __hidden_deformable_surface_edit::RegisterFullEdge(edges, indices[1], indices[2]);
        __hidden_deformable_surface_edit::RegisterFullEdge(edges, indices[2], indices[0]);
    }
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        if(removeTriangle[triangle] == 0u)
            continue;

        u32 indices[3] = {};
        if(!__hidden_deformable_surface_edit::ValidateTriangleIndex(instance, static_cast<u32>(triangle), indices))
            return false;

        if(!__hidden_deformable_surface_edit::RegisterRemovedEdge(edges, indices[0], indices[1])
            || !__hidden_deformable_surface_edit::RegisterRemovedEdge(edges, indices[1], indices[2])
            || !__hidden_deformable_surface_edit::RegisterRemovedEdge(edges, indices[2], indices[0])
        )
            return false;
    }

    Vector<__hidden_deformable_surface_edit::EdgeRecord> boundaryEdges;
    boundaryEdges.reserve(removedTriangleCount * 3u);
    __hidden_deformable_surface_edit::VertexDegreeMap boundaryDegrees;
    boundaryDegrees.reserve(removedTriangleCount * 3u);
    for(const auto& [edgeKey, edge] : edges){
        (void)edgeKey;
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
        (void)vertex;
        if(degree != 2u)
            return false;
    }
    if(!__hidden_deformable_surface_edit::BoundaryEdgesFormSingleLoop(boundaryEdges))
        return false;

    Vector<DeformableVertexRest> newRestVertices = instance.restVertices;
    Vector<SkinInfluence4> newSkin = instance.skin;
    Vector<SourceSample> newSourceSamples = instance.sourceSamples;
    Vector<DeformableMorph> newMorphs = instance.morphs;
    Vector<u32> newIndices;
    const usize removedIndexCount = static_cast<usize>(removedTriangleCount) * 3u;
    const usize wallIndexCount = __hidden_deformable_surface_edit::ActiveLength(params.depth)
        ? boundaryEdges.size() * 6u
        : 0u
    ;
    newIndices.reserve(instance.indices.size() - removedIndexCount + wallIndexCount);
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        if(removeTriangle[triangle] != 0u)
            continue;

        const usize indexBase = triangle * 3u;
        newIndices.push_back(instance.indices[indexBase + 0u]);
        newIndices.push_back(instance.indices[indexBase + 1u]);
        newIndices.push_back(instance.indices[indexBase + 2u]);
    }

    __hidden_deformable_surface_edit::InnerVertexMap innerVertices;
    innerVertices.reserve(boundaryEdges.size());
    auto ensureInnerVertex = [&](const u32 outerVertex, u32& outInnerVertex) -> bool{
        const auto foundInner = innerVertices.find(outerVertex);
        if(foundInner != innerVertices.end()){
            outInnerVertex = foundInner.value();
            return true;
        }
        if(newRestVertices.size() >= static_cast<usize>(Limit<u32>::s_Max))
            return false;

        DeformableVertexRest innerVertex = newRestVertices[outerVertex];
        const __hidden_deformable_surface_edit::Vec3 position =
            __hidden_deformable_surface_edit::Subtract(
                __hidden_deformable_surface_edit::ToVec3(innerVertex.position),
                __hidden_deformable_surface_edit::Scale(frame.normal, params.depth)
            )
        ;
        innerVertex.position = __hidden_deformable_surface_edit::ToFloat3(position);

        outInnerVertex = static_cast<u32>(newRestVertices.size());
        newRestVertices.push_back(innerVertex);
        if(!newSkin.empty())
            newSkin.push_back(newSkin[outerVertex]);
        if(!newSourceSamples.empty())
            newSourceSamples.push_back(newSourceSamples[outerVertex]);
        if(!__hidden_deformable_surface_edit::TransferMorphDeltasForInnerVertex(newMorphs, outerVertex, outInnerVertex))
            return false;
        innerVertices.emplace(outerVertex, outInnerVertex);
        return true;
    };

    u32 addedTriangleCount = 0;
    if(__hidden_deformable_surface_edit::ActiveLength(params.depth)){
        for(const __hidden_deformable_surface_edit::EdgeRecord& edge : boundaryEdges){
            u32 innerA = 0;
            u32 innerB = 0;
            if(!ensureInnerVertex(edge.a, innerA) || !ensureInnerVertex(edge.b, innerB))
                return false;

            newIndices.push_back(edge.a);
            newIndices.push_back(edge.b);
            newIndices.push_back(innerB);
            newIndices.push_back(edge.a);
            newIndices.push_back(innerB);
            newIndices.push_back(innerA);
            addedTriangleCount += 2u;
        }
    }

    instance.restVertices = Move(newRestVertices);
    instance.indices = Move(newIndices);
    instance.skin = Move(newSkin);
    instance.sourceSamples = Move(newSourceSamples);
    instance.morphs = Move(newMorphs);
    ++instance.editRevision;
    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(instance.dirtyFlags | RuntimeMeshDirtyFlag::All);

    if(outResult){
        outResult->removedTriangleCount = removedTriangleCount;
        outResult->addedVertexCount = static_cast<u32>(innerVertices.size());
        outResult->addedTriangleCount = addedTriangleCount;
        outResult->editRevision = instance.editRevision;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
