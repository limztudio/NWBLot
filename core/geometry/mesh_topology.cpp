// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_topology.h"

#include <core/alloc/scratch.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_mesh_topology{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_FrameEpsilon = 0.00000001f;

struct BoundaryVertexEdges{
    usize edgeIndices[2] = { Limit<usize>::s_Max, Limit<usize>::s_Max };
    u32 count = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FiniteVector(const SIMDVector value, const u32 activeMask){
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    return (VectorMoveMask(invalid) & activeMask) == 0u;
}

[[nodiscard]] bool ValidDirection(const SIMDVector value){
    return FiniteVector(value, 0x7u)
        && VectorGetX(Vector3LengthSq(value)) > s_FrameEpsilon
    ;
}

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
    return FiniteVector(center, 0x7u)
        && ValidDirection(tangent)
        && ValidDirection(bitangent)
        && Abs(VectorGetX(Vector3Dot(tangent, bitangent))) <= 0.001f
    ;
}

[[nodiscard]] bool ValidEdge(const MeshTopologyEdge& edge, const usize vertexCount){
    return edge.a < vertexCount
        && edge.b < vertexCount
        && edge.a != edge.b
    ;
}

[[nodiscard]] f32 ProjectedSignedLoopArea(
    const Vector<MeshTopologyEdge>& orderedEdges,
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

void ReverseBoundaryLoop(Vector<MeshTopologyEdge>& edges){
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

void CanonicalizeBoundaryLoopStart(Vector<MeshTopologyEdge>& edges){
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
    Vector<MeshTopologyEdge> orderedEdges;
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
    if(!IsFinite(signedArea) || Abs(signedArea) <= s_FrameEpsilon)
        return false;
    if(signedArea < 0.0f)
        ReverseBoundaryLoop(orderedEdges);
    CanonicalizeBoundaryLoopStart(orderedEdges);
    outOrderedEdges = Move(orderedEdges);
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

