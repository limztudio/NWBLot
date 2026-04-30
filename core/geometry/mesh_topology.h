// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshTopologyEdge{
    u32 a = 0;
    u32 b = 0;
    u32 fullCount = 0;
    u32 removedCount = 0;
};
static_assert(IsStandardLayout_V<MeshTopologyEdge>, "MeshTopologyEdge must stay layout-stable");
static_assert(IsTriviallyCopyable_V<MeshTopologyEdge>, "MeshTopologyEdge must stay cheap to copy");

struct MeshTopologyBoundaryLoopFrame{
    Float3U center;
    Float3U tangent;
    Float3U bitangent;
};
static_assert(IsStandardLayout_V<MeshTopologyBoundaryLoopFrame>, "MeshTopologyBoundaryLoopFrame must stay layout-stable");
static_assert(IsTriviallyCopyable_V<MeshTopologyBoundaryLoopFrame>, "MeshTopologyBoundaryLoopFrame must stay cheap to copy");

struct MeshTopologyLoopVertexFrame{
    Float3U normal;
    Float3U tangent;
};
static_assert(IsStandardLayout_V<MeshTopologyLoopVertexFrame>, "MeshTopologyLoopVertexFrame must stay layout-stable");
static_assert(IsTriviallyCopyable_V<MeshTopologyLoopVertexFrame>, "MeshTopologyLoopVertexFrame must stay cheap to copy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ValidMeshTopologyEdge(const MeshTopologyEdge& edge, const usize vertexCount){
    return
        edge.a < vertexCount
        && edge.b < vertexCount
        && edge.a != edge.b
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BuildOrderedBoundaryLoop(
    const Vector<MeshTopologyEdge>& boundaryEdges,
    const Vector<Float3U>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    Vector<MeshTopologyEdge>& outOrderedEdges
);

[[nodiscard]] bool BuildOrderedBoundaryLoop(
    const Vector<MeshTopologyEdge, Core::Alloc::ScratchAllocator<MeshTopologyEdge>>& boundaryEdges,
    const Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    Vector<MeshTopologyEdge, Core::Alloc::ScratchAllocator<MeshTopologyEdge>>& outOrderedEdges
);

[[nodiscard]] bool BuildBoundaryEdgesFromRemovedTriangles(
    const Vector<u32>& indices,
    const Vector<u8>& removedTriangles,
    Vector<MeshTopologyEdge>& outBoundaryEdges,
    u32* outRemovedTriangleCount = nullptr
);

[[nodiscard]] bool BuildBoundaryEdgesFromRemovedTriangles(
    const Vector<u32>& indices,
    const Vector<u8, Core::Alloc::ScratchAllocator<u8>>& removedTriangles,
    Vector<MeshTopologyEdge, Core::Alloc::ScratchAllocator<MeshTopologyEdge>>& outBoundaryEdges,
    u32* outRemovedTriangleCount = nullptr
);

[[nodiscard]] bool BuildBoundaryLoopVertexFrame(
    const Vector<Float3U>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    const MeshTopologyEdge& previousEdge,
    const MeshTopologyEdge& currentEdge,
    MeshTopologyLoopVertexFrame& outFrame
);

[[nodiscard]] bool BuildBoundaryLoopVertexFrame(
    const Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    const MeshTopologyEdge& previousEdge,
    const MeshTopologyEdge& currentEdge,
    MeshTopologyLoopVertexFrame& outFrame
);

[[nodiscard]] bool AppendWallTrianglePairs(
    const Vector<MeshTopologyEdge>& orderedBoundaryEdges,
    const Vector<u32>& innerVertices,
    Vector<u32>& outIndices,
    u32* outAddedTriangleCount = nullptr
);

[[nodiscard]] bool AppendWallTrianglePairs(
    const Vector<MeshTopologyEdge, Core::Alloc::ScratchAllocator<MeshTopologyEdge>>& orderedBoundaryEdges,
    const Vector<u32, Core::Alloc::ScratchAllocator<u32>>& innerVertices,
    Vector<u32>& outIndices,
    u32* outAddedTriangleCount = nullptr
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

