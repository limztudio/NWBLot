// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BuildOrderedBoundaryLoop(
    const Vector<MeshTopologyEdge>& boundaryEdges,
    const Vector<Float3U>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    Vector<MeshTopologyEdge>& outOrderedEdges
);

[[nodiscard]] bool AppendWallTrianglePairs(
    const Vector<MeshTopologyEdge>& orderedBoundaryEdges,
    const Vector<u32>& innerVertices,
    Vector<u32>& outIndices,
    u32* outAddedTriangleCount = nullptr
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

