// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_types.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Seam-safe, attribute-aware cap handling for deformable CSG rebuilds.
//
// Owns cut boundary loop collection, deterministic ordering, and fan fills
// with one cap orientation rule, so preview and commit caps always agree.
struct CsgDeformCutLoopEdge{
    u32 first = 0u;
    u32 second = 0u;
};

static_assert(IsStandardLayout_V<CsgDeformCutLoopEdge>, "CsgDeformCutLoopEdge must stay layout-stable for cap loops");
static_assert(IsTriviallyCopyable_V<CsgDeformCutLoopEdge>, "CsgDeformCutLoopEdge must stay cheap to copy in cap loops");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgDeformCapBuilder final : NoCopy{
public:
    CsgDeformCapBuilder() = delete;


public:
    static void CollectBoundaryEdges(
        Core::Alloc::ScratchArena& scratchArena,
        const CsgDeformTriangleVector<Core::Alloc::ScratchArena>& triangles,
        Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena>& outEdges
    );
    [[nodiscard]] static bool OrderBoundaryLoop(
        Core::Alloc::ScratchArena& scratchArena,
        const Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena>& edges,
        Vector<u32, Core::Alloc::ScratchArena>& outLoop
    );
    [[nodiscard]] static bool CapNormal(
        const CsgDeformVertexVector<Core::Alloc::ScratchArena>& vertices,
        const Vector<u32, Core::Alloc::ScratchArena>& loop,
        Float4& outNormal
    );
    [[nodiscard]] static bool FillCapLoop(
        const Float4& loopNormal,
        CsgDeformVertexVector<Core::Alloc::ScratchArena>& inOutVertices,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& inOutTriangles,
        const Vector<u32, Core::Alloc::ScratchArena>& loop,
        u32& outCapTriangles
    );
    [[nodiscard]] static bool FillCutCaps(
        Core::Alloc::ScratchArena& scratchArena,
        CsgDeformVertexVector<Core::Alloc::ScratchArena>& inOutVertices,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& inOutTriangles,
        Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena>& scratchEdges,
        u32& outCapTriangles
    );
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

