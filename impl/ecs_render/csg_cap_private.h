// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "csg_cap_builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderCsgCapDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_CapDistanceEpsilon = 0.00005f;
inline constexpr f32 s_PointMergeEpsilon = 0.0001f;
inline constexpr f32 s_PointMergeEpsilonSquared = s_PointMergeEpsilon * s_PointMergeEpsilon;
inline constexpr f32 s_NormalizeMinLengthSquared = 0.00000001f;
inline constexpr u32 s_EdgeIntersectionRefineIterations = 8u;


struct CapSourceVertex{
    SIMDVector position;
    SIMDVector normal;
    SIMDVector tangent;
    SIMDVector uv0;
    SIMDVector color;
};

struct CapPoint{
    Float4 position;
    Float4 normal;
    Float4 tangent;
    Float4 uv0;
    Float4 color;
};

struct CapEdge{
    u32 a = 0u;
    u32 b = 0u;
};

struct CapIntersection{
    CapSourceVertex vertices[3];
    u32 count = 0u;
};

struct CapProjectedPoint{
    u32 pointIndex = 0u;
    f32 u = 0.0f;
    f32 v = 0.0f;
};

struct CapCutterEval{
    const CsgCutterGpuData& cutter;
    SIMDMatrix worldToShape;
};

using CapPointVector = Vector<CapPoint, Core::Alloc::ScratchArena>;
using CapEdgeVector = Vector<CapEdge, Core::Alloc::ScratchArena>;
using CapIndexVector = Vector<u32, Core::Alloc::ScratchArena>;
using CapByteVector = Vector<u8, Core::Alloc::ScratchArena>;
using CapProjectedPointVector = Vector<CapProjectedPoint, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool CutterSupportsCap(u32 shapeType);
[[nodiscard]] SIMDVector EvaluateShapeDistance(const CapCutterEval& cutterEval, SIMDVector worldPosition);
[[nodiscard]] SIMDVector EvaluateWorldCapNormal(const CapCutterEval& cutterEval, SIMDVector worldPosition, SIMDVector fallback);

[[nodiscard]] bool BuildCapSegments(
    const CsgCapMeshTriangleVector& triangles,
    const Scene::TransformComponent* transform,
    const CapCutterEval& cutterEval,
    CapPointVector& points,
    CapEdgeVector& edges
);

[[nodiscard]] bool AppendCapTriangle(
    CsgCapVertexGpuDataVector& vertices,
    const CapPointVector& points,
    const CapCutterEval& cutterEval,
    u32 a,
    u32 b,
    u32 c,
    u32 receiverIndex,
    u32 cutterIndex
);

[[nodiscard]] bool AppendEarClippedTriangulation(
    CsgCapVertexGpuDataVector& vertices,
    const CapPointVector& points,
    const CapIndexVector& loop,
    const CapCutterEval& cutterEval,
    u32 receiverIndex,
    u32 cutterIndex,
    Core::Alloc::ScratchArena& scratchArena
);

[[nodiscard]] bool AppendCapLoops(
    CsgCapVertexGpuDataVector& vertices,
    const CapPointVector& points,
    const CapEdgeVector& edges,
    const CapCutterEval& cutterEval,
    u32 receiverIndex,
    u32 cutterIndex,
    Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

