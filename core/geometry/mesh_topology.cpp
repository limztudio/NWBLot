// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_topology.h"

#include "frame_math.h"

#include <core/alloc/scratch.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_mesh_topology{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BoundaryVertexEdges{
    usize edgeIndices[2] = { Limit<usize>::s_Max, Limit<usize>::s_Max };
    u32 count = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool RegisterBoundaryVertexEdge(
    HashMap<
        u32,
        BoundaryVertexEdges,
        Hasher<u32>,
        EqualTo<u32>,
        Core::Alloc::ScratchAllocator<Pair<const u32, BoundaryVertexEdges>>
    >& vertexEdges,
    const u32 vertex,
    const usize edgeIndex
){
    auto it = vertexEdges.emplace(vertex, BoundaryVertexEdges{}).first;
    BoundaryVertexEdges& adjacency = it.value();
    if(adjacency.count >= LengthOf(adjacency.edgeIndices))
        return false;

    adjacency.edgeIndices[adjacency.count] = edgeIndex;
    ++adjacency.count;
    return true;
}

[[nodiscard]] bool ValidLoopFrame(const MeshTopologyBoundaryLoopFrame& frame){
    const SIMDVector center = LoadFloat(frame.center);
    const SIMDVector tangent = LoadFloat(frame.tangent);
    const SIMDVector bitangent = LoadFloat(frame.bitangent);
    return FrameFiniteVector(center, 0x7u)
        && FrameValidDirection(tangent)
        && FrameValidDirection(bitangent)
        && Abs(VectorGetX(Vector3Dot(tangent, bitangent))) <= 0.001f
    ;
}

[[nodiscard]] bool ValidEdge(const MeshTopologyEdge& edge, const usize vertexCount){
    return edge.a < vertexCount
        && edge.b < vertexCount
        && edge.a != edge.b
    ;
}

[[nodiscard]] u64 MakeEdgeKey(const u32 a, const u32 b){
    const u32 lo = a < b ? a : b;
    const u32 hi = a < b ? b : a;
    return (static_cast<u64>(lo) << 32u) | static_cast<u64>(hi);
}

template<typename EdgeMap>
void RegisterFullEdge(EdgeMap& edges, const u32 a, const u32 b){
    auto [it, inserted] = edges.emplace(MakeEdgeKey(a, b), MeshTopologyEdge{});
    MeshTopologyEdge& record = it.value();
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

    MeshTopologyEdge& record = found.value();
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

template<typename EdgeAllocator>
[[nodiscard]] f32 ProjectedSignedLoopArea(
    const Vector<MeshTopologyEdge, EdgeAllocator>& orderedEdges,
    const Vector<Float3U>& positions,
    const MeshTopologyBoundaryLoopFrame& frame
){
    const SIMDVector center = LoadFloat(frame.center);
    const SIMDVector tangent = LoadFloat(frame.tangent);
    const SIMDVector bitangent = LoadFloat(frame.bitangent);
    f32 signedArea = 0.0f;
    for(const MeshTopologyEdge& edge : orderedEdges){
        const SIMDVector aOffset = VectorSubtract(LoadFloat(positions[edge.a]), center);
        const SIMDVector bOffset = VectorSubtract(LoadFloat(positions[edge.b]), center);
        const f32 ax = VectorGetX(Vector3Dot(aOffset, tangent));
        const f32 ay = VectorGetX(Vector3Dot(aOffset, bitangent));
        const f32 bx = VectorGetX(Vector3Dot(bOffset, tangent));
        const f32 by = VectorGetX(Vector3Dot(bOffset, bitangent));
        signedArea += (ax * by) - (bx * ay);
    }
    return signedArea * 0.5f;
}

template<typename EdgeAllocator>
void ReverseBoundaryLoop(Vector<MeshTopologyEdge, EdgeAllocator>& edges){
    if(edges.empty())
        return;

    usize left = 0u;
    usize right = edges.size() - 1u;
    while(left < right){
        MeshTopologyEdge leftEdge = edges[left];
        MeshTopologyEdge rightEdge = edges[right];
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

template<typename EdgeAllocator>
void CanonicalizeBoundaryLoopStart(Vector<MeshTopologyEdge, EdgeAllocator>& edges){
    if(edges.empty())
        return;

    usize startEdgeIndex = 0u;
    for(usize edgeIndex = 1u; edgeIndex < edges.size(); ++edgeIndex){
        const MeshTopologyEdge& edge = edges[edgeIndex];
        const MeshTopologyEdge& startEdge = edges[startEdgeIndex];
        if(edge.a < startEdge.a || (edge.a == startEdge.a && edge.b < startEdge.b))
            startEdgeIndex = edgeIndex;
    }
    if(startEdgeIndex == 0u)
        return;

    Rotate(edges.begin(), edges.begin() + static_cast<isize>(startEdgeIndex), edges.end());
}

[[nodiscard]] SIMDVector LoopFrameNormal(const MeshTopologyBoundaryLoopFrame& frame){
    const SIMDVector tangent = FrameNormalizeDirection(LoadFloat(frame.tangent), VectorSet(1.0f, 0.0f, 0.0f, 0.0f));
    const SIMDVector bitangent = FrameNormalizeDirection(LoadFloat(frame.bitangent), VectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    return FrameNormalizeDirection(Vector3Cross(tangent, bitangent), VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
}

[[nodiscard]] SIMDVector ProjectedEdgeDirection(
    const Vector<Float3U>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    const SIMDVector frameNormal,
    const MeshTopologyEdge& edge)
{
    return FrameResolveTangent(
        frameNormal,
        VectorSubtract(LoadFloat(positions[edge.b]), LoadFloat(positions[edge.a])),
        LoadFloat(frame.tangent)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildOrderedBoundaryLoop(
    const Vector<MeshTopologyEdge>& boundaryEdges,
    const Vector<Float3U>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    Vector<MeshTopologyEdge>& outOrderedEdges
){
    using namespace __hidden_geometry_mesh_topology;

    outOrderedEdges.clear();
    if(boundaryEdges.empty() || positions.empty() || !ValidLoopFrame(frame))
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
        const MeshTopologyEdge& edge = boundaryEdges[edgeIndex];
        if(!ValidEdge(edge, positions.size())
            || !RegisterBoundaryVertexEdge(vertexEdges, edge.a, edgeIndex)
            || !RegisterBoundaryVertexEdge(vertexEdges, edge.b, edgeIndex)
        )
            return false;
    }
    for(const auto& [vertex, adjacency] : vertexEdges){
        static_cast<void>(vertex);
        if(adjacency.count != 2u)
            return false;
    }

    const u32 startVertex = boundaryEdges[0].a;
    u32 currentVertex = startVertex;
    Vector<MeshTopologyEdge, Core::Alloc::ScratchAllocator<MeshTopologyEdge>> orderedEdges{
        Core::Alloc::ScratchAllocator<MeshTopologyEdge>(scratchArena)
    };
    orderedEdges.reserve(boundaryEdges.size());
    while(orderedEdges.size() < boundaryEdges.size()){
        usize nextEdgeIndex = Limit<usize>::s_Max;
        MeshTopologyEdge nextEdge;
        const auto foundEdges = vertexEdges.find(currentVertex);
        if(foundEdges == vertexEdges.end())
            return false;

        const BoundaryVertexEdges& adjacentEdges = foundEdges.value();
        for(u32 adjacencyIndex = 0u; adjacencyIndex < adjacentEdges.count; ++adjacencyIndex){
            const usize edgeIndex = adjacentEdges.edgeIndices[adjacencyIndex];
            if(edgeIndex >= boundaryEdges.size() || visitedEdges[edgeIndex] != 0u)
                continue;

            const MeshTopologyEdge& edge = boundaryEdges[edgeIndex];
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
        orderedEdges.push_back(nextEdge);
        currentVertex = nextEdge.b;
        if(currentVertex == startVertex && orderedEdges.size() != boundaryEdges.size())
            return false;
    }

    if(currentVertex != startVertex)
        return false;

    const f32 signedArea = ProjectedSignedLoopArea(orderedEdges, positions, frame);
    if(!IsFinite(signedArea) || Abs(signedArea) <= s_FrameDirectionEpsilon)
        return false;
    if(signedArea < 0.0f)
        ReverseBoundaryLoop(orderedEdges);
    CanonicalizeBoundaryLoopStart(orderedEdges);
    outOrderedEdges.assign(orderedEdges.begin(), orderedEdges.end());
    return true;
}

bool BuildBoundaryEdgesFromRemovedTriangles(
    const Vector<u32>& indices,
    const Vector<u8>& removedTriangles,
    Vector<MeshTopologyEdge>& outBoundaryEdges,
    u32* outRemovedTriangleCount)
{
    using namespace __hidden_geometry_mesh_topology;

    if(outRemovedTriangleCount)
        *outRemovedTriangleCount = 0u;
    outBoundaryEdges.clear();

    if(indices.empty()
        || (indices.size() % 3u) != 0u
        || indices.size() / 3u != removedTriangles.size()
    )
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    using EdgeRecordMap = HashMap<
        u64,
        MeshTopologyEdge,
        Hasher<u64>,
        EqualTo<u64>,
        Core::Alloc::ScratchAllocator<Pair<const u64, MeshTopologyEdge>>
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
        Core::Alloc::ScratchAllocator<Pair<const u64, MeshTopologyEdge>>(scratchArena)
    );
    edges.reserve(indices.size());

    const usize triangleCount = indices.size() / 3u;
    u32 removedTriangleCount = 0u;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        const usize indexBase = triangle * 3u;
        const u32 a = indices[indexBase + 0u];
        const u32 b = indices[indexBase + 1u];
        const u32 c = indices[indexBase + 2u];
        if(a == b || a == c || b == c)
            return false;

        RegisterFullEdge(edges, a, b);
        RegisterFullEdge(edges, b, c);
        RegisterFullEdge(edges, c, a);

        if(removedTriangles[triangle] == 0u)
            continue;

        ++removedTriangleCount;
        if(!RegisterRemovedEdge(edges, a, b)
            || !RegisterRemovedEdge(edges, b, c)
            || !RegisterRemovedEdge(edges, c, a)
        )
            return false;
    }
    if(removedTriangleCount == 0u || removedTriangleCount >= triangleCount)
        return false;

    Vector<MeshTopologyEdge, Core::Alloc::ScratchAllocator<MeshTopologyEdge>> boundaryEdges{
        Core::Alloc::ScratchAllocator<MeshTopologyEdge>(scratchArena)
    };
    boundaryEdges.reserve(static_cast<usize>(removedTriangleCount) * 3u);
    VertexDegreeMap boundaryDegrees(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        Core::Alloc::ScratchAllocator<Pair<const u32, u32>>(scratchArena)
    );
    boundaryDegrees.reserve(static_cast<usize>(removedTriangleCount) * 3u);

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

    outBoundaryEdges.assign(boundaryEdges.begin(), boundaryEdges.end());
    if(outRemovedTriangleCount)
        *outRemovedTriangleCount = removedTriangleCount;
    return true;
}

bool BuildBoundaryLoopVertexFrame(
    const Vector<Float3U>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    const MeshTopologyEdge& previousEdge,
    const MeshTopologyEdge& currentEdge,
    MeshTopologyLoopVertexFrame& outFrame)
{
    using namespace __hidden_geometry_mesh_topology;

    outFrame = MeshTopologyLoopVertexFrame{};
    if(positions.empty()
        || !ValidLoopFrame(frame)
        || !ValidEdge(previousEdge, positions.size())
        || !ValidEdge(currentEdge, positions.size())
        || previousEdge.b != currentEdge.a
    )
        return false;

    const SIMDVector frameNormal = LoopFrameNormal(frame);
    if(!FrameValidDirection(frameNormal))
        return false;

    const SIMDVector previousDirection = ProjectedEdgeDirection(positions, frame, frameNormal, previousEdge);
    const SIMDVector currentDirection = ProjectedEdgeDirection(positions, frame, frameNormal, currentEdge);
    if(!FrameValidDirection(previousDirection) || !FrameValidDirection(currentDirection))
        return false;

    const SIMDVector previousInwardVector = FrameNormalizeDirection(
        Vector3Cross(frameNormal, previousDirection),
        LoadFloat(frame.bitangent)
    );
    const SIMDVector currentInwardVector = FrameNormalizeDirection(
        Vector3Cross(frameNormal, currentDirection),
        previousInwardVector
    );
    SIMDVector normalVector = FrameNormalizeDirection(
        VectorAdd(previousInwardVector, currentInwardVector),
        currentInwardVector
    );
    if(!FrameValidDirection(normalVector))
        return false;

    const SIMDVector centerOffset = VectorSubtract(LoadFloat(frame.center), LoadFloat(positions[currentEdge.a]));
    if(VectorGetX(Vector3Dot(normalVector, centerOffset)) < 0.0f)
        normalVector = VectorScale(normalVector, -1.0f);

    const SIMDVector tangentVector = FrameResolveTangent(
        normalVector,
        Vector3Cross(frameNormal, normalVector),
        currentDirection
    );
    if(!FrameValidDirection(tangentVector) || Abs(VectorGetX(Vector3Dot(normalVector, tangentVector))) > 0.001f)
        return false;

    StoreFloat(normalVector, &outFrame.normal);
    StoreFloat(tangentVector, &outFrame.tangent);
    return true;
}

bool AppendWallTrianglePairs(
    const Vector<MeshTopologyEdge>& orderedBoundaryEdges,
    const Vector<u32>& innerVertices,
    Vector<u32>& outIndices,
    u32* outAddedTriangleCount
){
    if(outAddedTriangleCount)
        *outAddedTriangleCount = 0u;
    if(orderedBoundaryEdges.size() < 3u
        || orderedBoundaryEdges.size() != innerVertices.size()
        || orderedBoundaryEdges.size() > static_cast<usize>(Limit<u32>::s_Max / 2u)
        || orderedBoundaryEdges.size() > static_cast<usize>(Limit<usize>::s_Max / 6u)
        || (orderedBoundaryEdges.size() * 6u) > Limit<usize>::s_Max - outIndices.size()
    )
        return false;

    const usize boundaryVertexCount = orderedBoundaryEdges.size();
    for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
        const usize nextEdgeIndex = (edgeIndex + 1u) % boundaryVertexCount;
        if(orderedBoundaryEdges[edgeIndex].b != orderedBoundaryEdges[nextEdgeIndex].a)
            return false;
        if(innerVertices[edgeIndex] == orderedBoundaryEdges[edgeIndex].a
            || innerVertices[edgeIndex] == orderedBoundaryEdges[edgeIndex].b
            || innerVertices[edgeIndex] == innerVertices[nextEdgeIndex]
        )
            return false;
    }

    outIndices.reserve(outIndices.size() + boundaryVertexCount * 6u);
    for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
        const usize nextEdgeIndex = (edgeIndex + 1u) % boundaryVertexCount;
        const u32 rimA = orderedBoundaryEdges[edgeIndex].a;
        const u32 rimB = orderedBoundaryEdges[nextEdgeIndex].a;
        const u32 innerB = innerVertices[nextEdgeIndex];
        const u32 innerA = innerVertices[edgeIndex];

        outIndices.push_back(rimA);
        outIndices.push_back(rimB);
        outIndices.push_back(innerB);
        outIndices.push_back(rimA);
        outIndices.push_back(innerB);
        outIndices.push_back(innerA);
    }

    if(outAddedTriangleCount)
        *outAddedTriangleCount = static_cast<u32>(boundaryVertexCount * 2u);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

