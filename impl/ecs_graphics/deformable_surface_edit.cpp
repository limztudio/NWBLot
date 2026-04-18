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

[[nodiscard]] bool NearlyOne(const f32 value){
    return AbsF32(value - 1.0f) <= 0.001f;
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

[[nodiscard]] bool ValidSkinInfluence(const SkinInfluence4& skin){
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

[[nodiscard]] bool ValidSourceSample(const SourceSample& sample, const u32 sourceTriangleCount){
    return sourceTriangleCount != 0u && sample.sourceTri < sourceTriangleCount && ValidBarycentric(sample.bary);
}

[[nodiscard]] bool ValidMorphDelta(const DeformableMorphDelta& delta, const usize vertexCount){
    return delta.vertexId < vertexCount
        && IsFiniteFloat3(delta.deltaPosition)
        && IsFiniteFloat3(delta.deltaNormal)
        && IsFiniteFloat4(delta.deltaTangent)
    ;
}

[[nodiscard]] bool ValidateRuntimePayloadArrays(
    const Vector<DeformableVertexRest>& restVertices,
    const Vector<u32>& indices,
    const u32 sourceTriangleCount,
    const Vector<SkinInfluence4>& skin,
    const Vector<SourceSample>& sourceSamples,
    const Vector<DeformableMorph>& morphs)
{
    if(restVertices.empty() || indices.empty())
        return false;
    if(restVertices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || indices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || (indices.size() % 3u) != 0u
    )
        return false;
    if(!skin.empty() && skin.size() != restVertices.size())
        return false;
    if(!sourceSamples.empty() && sourceSamples.size() != restVertices.size())
        return false;
    if(!sourceSamples.empty() && sourceTriangleCount == 0u)
        return false;

    for(const DeformableVertexRest& vertex : restVertices){
        if(!ValidRestVertex(vertex))
            return false;
    }
    for(const u32 index : indices){
        if(index >= restVertices.size())
            return false;
    }
    for(const SkinInfluence4& influence : skin){
        if(!ValidSkinInfluence(influence))
            return false;
    }
    for(const SourceSample& sample : sourceSamples){
        if(!ValidSourceSample(sample, sourceTriangleCount))
            return false;
    }
    for(const DeformableMorph& morph : morphs){
        for(const DeformableMorphDelta& delta : morph.deltas){
            if(!ValidMorphDelta(delta, restVertices.size()))
                return false;
        }
    }
    return true;
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

[[nodiscard]] Vec3 FallbackTangent(const Vec3& normal){
    const Vec3 axis = AbsF32(normal.z) < 0.999f
        ? Vec3{ 0.0f, 0.0f, 1.0f }
        : Vec3{ 0.0f, 1.0f, 0.0f }
    ;
    return Normalize(Cross(axis, normal), Vec3{ 1.0f, 0.0f, 0.0f });
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

    const Vec3 rawNormal = Cross(edge0, edge1);
    if(LengthSquared(rawNormal) <= s_FrameEpsilon)
        return false;

    outFrame.center = BarycentricPoint(instance, triangleIndices, bary);
    outFrame.normal = Normalize(rawNormal, Vec3{ 0.0f, 0.0f, 1.0f });

    Vec3 tangent{
        instance.restVertices[triangleIndices[0]].tangent.x,
        instance.restVertices[triangleIndices[0]].tangent.y,
        instance.restVertices[triangleIndices[0]].tangent.z,
    };
    tangent = Subtract(tangent, Scale(outFrame.normal, Dot(tangent, outFrame.normal)));
    if(LengthSquared(tangent) <= s_FrameEpsilon)
        tangent = Subtract(edge0, Scale(outFrame.normal, Dot(edge0, outFrame.normal)));
    if(LengthSquared(tangent) <= s_FrameEpsilon)
        tangent = FallbackTangent(outFrame.normal);

    outFrame.tangent = Normalize(tangent, FallbackTangent(outFrame.normal));
    outFrame.bitangent = Normalize(Cross(outFrame.normal, outFrame.tangent), Vec3{ 0.0f, 1.0f, 0.0f });
    return LengthSquared(outFrame.normal) > s_FrameEpsilon
        && LengthSquared(outFrame.tangent) > s_FrameEpsilon
        && LengthSquared(outFrame.bitangent) > s_FrameEpsilon
        && AbsF32(Dot(outFrame.normal, outFrame.tangent)) <= 0.001f
    ;
}

[[nodiscard]] bool ValidateRuntimePayload(const DeformableRuntimeMeshInstance& instance){
    if(!instance.entity.valid() || !instance.handle.valid() || instance.restVertices.empty() || instance.indices.empty())
        return false;
    if(!ValidDeformableDisplacementDescriptor(instance.displacement))
        return false;
    return ValidateRuntimePayloadArrays(
        instance.restVertices,
        instance.indices,
        instance.sourceTriangleCount,
        instance.skin,
        instance.sourceSamples,
        instance.morphs
    );
}

[[nodiscard]] bool ValidateParams(const DeformableRuntimeMeshInstance& instance, const DeformableHoleEditParams& params){
    if(!ValidBarycentric(params.posedHit.bary))
        return false;
    if(params.posedHit.entity != instance.entity)
        return false;
    if(params.posedHit.runtimeMesh != instance.handle)
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

[[nodiscard]] f32 ProjectedSignedLoopArea(
    const DeformableRuntimeMeshInstance& instance,
    const HoleFrame& frame,
    const Vector<EdgeRecord>& orderedEdges)
{
    f32 signedArea = 0.0f;
    for(const EdgeRecord& edge : orderedEdges){
        const Vec3 aOffset = Subtract(ToVec3(instance.restVertices[edge.a].position), frame.center);
        const Vec3 bOffset = Subtract(ToVec3(instance.restVertices[edge.b].position), frame.center);
        const f32 ax = Dot(aOffset, frame.tangent);
        const f32 ay = Dot(aOffset, frame.bitangent);
        const f32 bx = Dot(bOffset, frame.tangent);
        const f32 by = Dot(bOffset, frame.bitangent);
        signedArea += (ax * by) - (bx * ay);
    }
    return signedArea * 0.5f;
}

void ReverseBoundaryLoop(Vector<EdgeRecord>& edges){
    Vector<EdgeRecord> reversedEdges;
    reversedEdges.reserve(edges.size());
    for(usize edgeIndex = edges.size(); edgeIndex > 0u; --edgeIndex){
        EdgeRecord edge = edges[edgeIndex - 1u];
        const u32 a = edge.a;
        edge.a = edge.b;
        edge.b = a;
        reversedEdges.push_back(edge);
    }
    edges = Move(reversedEdges);
}

void CanonicalizeBoundaryLoopStart(Vector<EdgeRecord>& edges){
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

    Vector<EdgeRecord> rotatedEdges;
    rotatedEdges.reserve(edges.size());
    for(usize edgeOffset = 0u; edgeOffset < edges.size(); ++edgeOffset)
        rotatedEdges.push_back(edges[(startEdgeIndex + edgeOffset) % edges.size()]);
    edges = Move(rotatedEdges);
}

[[nodiscard]] bool BuildOrderedBoundaryLoop(
    const Vector<EdgeRecord>& boundaryEdges,
    const DeformableRuntimeMeshInstance& instance,
    const HoleFrame& frame,
    Vector<EdgeRecord>& outOrderedEdges)
{
    outOrderedEdges.clear();
    if(boundaryEdges.empty())
        return false;

    Vector<u8> visitedEdges;
    visitedEdges.resize(boundaryEdges.size(), 0u);

    const u32 startVertex = boundaryEdges[0].a;
    u32 currentVertex = startVertex;
    outOrderedEdges.reserve(boundaryEdges.size());
    while(outOrderedEdges.size() < boundaryEdges.size()){
        usize nextEdgeIndex = Limit<usize>::s_Max;
        EdgeRecord nextEdge;
        for(usize edgeIndex = 0; edgeIndex < boundaryEdges.size(); ++edgeIndex){
            if(visitedEdges[edgeIndex] != 0u)
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
    if(!IsFinite(signedArea) || AbsF32(signedArea) <= s_FrameEpsilon)
        return false;
    if(signedArea < 0.0f)
        ReverseBoundaryLoop(outOrderedEdges);
    CanonicalizeBoundaryLoopStart(outOrderedEdges);
    return true;
}

[[nodiscard]] bool TransferMorphDeltasForCopiedVertex(
    Vector<DeformableMorph>& morphs,
    const u32 sourceVertex,
    const u32 copiedVertex)
{
    for(DeformableMorph& morph : morphs){
        const usize sourceDeltaCount = morph.deltas.size();
        for(usize deltaIndex = 0; deltaIndex < sourceDeltaCount; ++deltaIndex){
            if(morph.deltas[deltaIndex].vertexId != sourceVertex)
                continue;
            if(morph.deltas.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;

            DeformableMorphDelta delta = morph.deltas[deltaIndex];
            delta.vertexId = copiedVertex;
            morph.deltas.push_back(delta);
        }
    }
    return true;
}

[[nodiscard]] f32 TangentHandedness(const f32 value){
    return value < 0.0f ? -1.0f : 1.0f;
}

[[nodiscard]] bool AppendWallVertex(
    Vector<DeformableVertexRest>& vertices,
    Vector<SkinInfluence4>& skin,
    Vector<SourceSample>& sourceSamples,
    Vector<DeformableMorph>& morphs,
    const u32 sourceVertex,
    const Vec3& position,
    const Vec3& normal,
    const Vec3& tangent,
    const f32 uvU,
    const f32 uvV,
    u32& outVertex)
{
    if(sourceVertex >= vertices.size() || vertices.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(!skin.empty() && sourceVertex >= skin.size())
        return false;
    if(!sourceSamples.empty() && sourceVertex >= sourceSamples.size())
        return false;

    DeformableVertexRest wallVertex = vertices[sourceVertex];
    wallVertex.position = ToFloat3(position);
    wallVertex.normal = ToFloat3(normal);
    wallVertex.tangent.x = tangent.x;
    wallVertex.tangent.y = tangent.y;
    wallVertex.tangent.z = tangent.z;
    wallVertex.tangent.w = TangentHandedness(wallVertex.tangent.w);
    wallVertex.uv0 = Float2Data(uvU, uvV);
    if(!ValidRestVertex(wallVertex))
        return false;

    outVertex = static_cast<u32>(vertices.size());
    vertices.push_back(wallVertex);
    if(!skin.empty())
        skin.push_back(skin[sourceVertex]);
    if(!sourceSamples.empty())
        sourceSamples.push_back(sourceSamples[sourceVertex]);
    return TransferMorphDeltasForCopiedVertex(morphs, sourceVertex, outVertex);
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
    Vector<__hidden_deformable_surface_edit::EdgeRecord> orderedBoundaryEdges;
    if(!__hidden_deformable_surface_edit::BuildOrderedBoundaryLoop(boundaryEdges, instance, frame, orderedBoundaryEdges))
        return false;

    Vector<DeformableVertexRest> newRestVertices = instance.restVertices;
    Vector<SkinInfluence4> newSkin = instance.skin;
    Vector<SourceSample> newSourceSamples = instance.sourceSamples;
    Vector<DeformableMorph> newMorphs = instance.morphs;
    Vector<u32> newIndices;
    const usize removedIndexCount = static_cast<usize>(removedTriangleCount) * 3u;
    const usize wallIndexCount = __hidden_deformable_surface_edit::ActiveLength(params.depth)
        ? orderedBoundaryEdges.size() * 6u
        : 0u
    ;
    const usize keptIndexCount = instance.indices.size() - removedIndexCount;
    if(wallIndexCount > Limit<usize>::s_Max - keptIndexCount
        || keptIndexCount + wallIndexCount > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    newIndices.reserve(keptIndexCount + wallIndexCount);
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        if(removeTriangle[triangle] != 0u)
            continue;

        const usize indexBase = triangle * 3u;
        newIndices.push_back(instance.indices[indexBase + 0u]);
        newIndices.push_back(instance.indices[indexBase + 1u]);
        newIndices.push_back(instance.indices[indexBase + 2u]);
    }

    u32 addedTriangleCount = 0;
    u32 addedVertexCount = 0;
    if(__hidden_deformable_surface_edit::ActiveLength(params.depth)){
        for(const __hidden_deformable_surface_edit::EdgeRecord& edge : orderedBoundaryEdges){
            const __hidden_deformable_surface_edit::Vec3 outerAPosition =
                __hidden_deformable_surface_edit::ToVec3(newRestVertices[edge.a].position)
            ;
            const __hidden_deformable_surface_edit::Vec3 outerBPosition =
                __hidden_deformable_surface_edit::ToVec3(newRestVertices[edge.b].position)
            ;
            __hidden_deformable_surface_edit::Vec3 edgeDirection =
                __hidden_deformable_surface_edit::Subtract(outerBPosition, outerAPosition)
            ;
            edgeDirection = __hidden_deformable_surface_edit::Subtract(
                edgeDirection,
                __hidden_deformable_surface_edit::Scale(frame.normal, __hidden_deformable_surface_edit::Dot(edgeDirection, frame.normal))
            );
            edgeDirection = __hidden_deformable_surface_edit::Normalize(edgeDirection, frame.tangent);

            const __hidden_deformable_surface_edit::Vec3 wallNormal =
                __hidden_deformable_surface_edit::Normalize(
                    __hidden_deformable_surface_edit::Cross(frame.normal, edgeDirection),
                    frame.bitangent
                )
            ;
            const __hidden_deformable_surface_edit::Vec3 innerAPosition =
                __hidden_deformable_surface_edit::Subtract(
                    outerAPosition,
                    __hidden_deformable_surface_edit::Scale(frame.normal, params.depth)
                )
            ;
            const __hidden_deformable_surface_edit::Vec3 innerBPosition =
                __hidden_deformable_surface_edit::Subtract(
                    outerBPosition,
                    __hidden_deformable_surface_edit::Scale(frame.normal, params.depth)
                )
            ;

            u32 rimA = 0;
            u32 rimB = 0;
            u32 innerB = 0;
            u32 innerA = 0;
            if(!__hidden_deformable_surface_edit::AppendWallVertex(
                    newRestVertices,
                    newSkin,
                    newSourceSamples,
                    newMorphs,
                    edge.a,
                    outerAPosition,
                    wallNormal,
                    edgeDirection,
                    0.0f,
                    0.0f,
                    rimA
                )
                || !__hidden_deformable_surface_edit::AppendWallVertex(
                    newRestVertices,
                    newSkin,
                    newSourceSamples,
                    newMorphs,
                    edge.b,
                    outerBPosition,
                    wallNormal,
                    edgeDirection,
                    1.0f,
                    0.0f,
                    rimB
                )
                || !__hidden_deformable_surface_edit::AppendWallVertex(
                    newRestVertices,
                    newSkin,
                    newSourceSamples,
                    newMorphs,
                    edge.b,
                    innerBPosition,
                    wallNormal,
                    edgeDirection,
                    1.0f,
                    1.0f,
                    innerB
                )
                || !__hidden_deformable_surface_edit::AppendWallVertex(
                    newRestVertices,
                    newSkin,
                    newSourceSamples,
                    newMorphs,
                    edge.a,
                    innerAPosition,
                    wallNormal,
                    edgeDirection,
                    0.0f,
                    1.0f,
                    innerA
                )
            )
                return false;

            newIndices.push_back(rimA);
            newIndices.push_back(rimB);
            newIndices.push_back(innerB);
            newIndices.push_back(rimA);
            newIndices.push_back(innerB);
            newIndices.push_back(innerA);
            addedVertexCount += 4u;
            addedTriangleCount += 2u;
        }
    }

    if(!__hidden_deformable_surface_edit::ValidateRuntimePayloadArrays(
            newRestVertices,
            newIndices,
            instance.sourceTriangleCount,
            newSkin,
            newSourceSamples,
            newMorphs
        )
    )
        return false;

    instance.restVertices = Move(newRestVertices);
    instance.indices = Move(newIndices);
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
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
