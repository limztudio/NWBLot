// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "mesh_topology.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SurfacePatchWallVertex{
    u32 sourceVertex = 0;
    u32 attributeVertices[3] = {};
    Float3U position;
    Float3U normal;
    Float3U tangent;
    Float2U uv0;
};
static_assert(IsStandardLayout_V<SurfacePatchWallVertex>, "SurfacePatchWallVertex must stay layout-stable");
static_assert(IsTriviallyCopyable_V<SurfacePatchWallVertex>, "SurfacePatchWallVertex must stay cheap to copy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BuildSurfacePatchLoopDistances(
    const Vector<MeshTopologyEdge>& orderedBoundaryEdges,
    const Vector<Float3U>& positions,
    const Float3U& frameNormal,
    f32* outLoopDistances,
    usize loopDistanceCount,
    f32& outLoopLength
);

[[nodiscard]] bool BuildSurfacePatchRingEdges(
    const u32* ringVertices,
    usize ringVertexCount,
    Vector<MeshTopologyEdge>& outEdges
);

[[nodiscard]] bool BuildSurfacePatchRingEdges(
    const u32* ringVertices,
    usize ringVertexCount,
    Vector<MeshTopologyEdge, Core::Alloc::ScratchAllocator<MeshTopologyEdge>>& outEdges
);

[[nodiscard]] bool AppendSurfacePatchCapTriangles(
    const u32* capVertices,
    usize capVertexCount,
    const Float3U* positions,
    usize positionCount,
    const Float3U& tangent,
    const Float3U& bitangent,
    Vector<u32>& outIndices,
    u32* outAddedTriangleCount = nullptr
);

[[nodiscard]] bool AppendSurfacePatchCapTriangles(
    const u32* capVertices,
    usize capVertexCount,
    const Float3U* positions,
    usize positionCount,
    SIMDVector tangent,
    SIMDVector bitangent,
    Vector<u32, Core::Alloc::ScratchAllocator<u32>>& outIndices,
    u32* outAddedTriangleCount = nullptr
);

[[nodiscard]] bool BuildSurfacePatchWallVertices(
    const Vector<MeshTopologyEdge>& orderedBoundaryEdges,
    const Vector<Float3U>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    const Float3U& frameNormal,
    f32 depth,
    usize wallBandCount,
    SurfacePatchWallVertex* outVertices,
    usize outVertexCount
);

[[nodiscard]] bool BuildSurfacePatchWallVertices(
    const Vector<MeshTopologyEdge, Core::Alloc::ScratchAllocator<MeshTopologyEdge>>& orderedBoundaryEdges,
    const Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    SIMDVector frameNormal,
    f32 depth,
    usize wallBandCount,
    SurfacePatchWallVertex* outVertices,
    usize outVertexCount
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

