// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns cut boundary loop collection, deterministic ordering, and fan fills with one cap orientation rule, so preview and commit caps always agree.
struct CsgDeformCutLoopEdge{
    u32 first = 0u;
    u32 second = 0u;
};

static_assert(IsStandardLayout_V<CsgDeformCutLoopEdge>, "CsgDeformCutLoopEdge must stay layout-stable for cap loops");
static_assert(IsTriviallyCopyable_V<CsgDeformCutLoopEdge>, "CsgDeformCutLoopEdge must stay cheap to copy in cap loops");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgDeformCapBuilder final : NoCopy{
public:
    // SIMD-domain cores: inputs and outputs stay on vector lanes, never touch storage.
    [[nodiscard]] static SIMDVectorSingle AccumulateFanAreaVec(SIMDVectorSingle inAreaVec, SIMDVectorSingle originVec, SIMDVectorSingle firstVec, SIMDVectorSingle secondVec)noexcept;
    static void CollectBoundaryEdges(Core::Alloc::ScratchArena& scratchArena, const CsgDeformTriangleVector<Core::Alloc::ScratchArena>& triangles, Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena>& outEdges);
    [[nodiscard]] static bool OrderBoundaryLoop(Core::Alloc::ScratchArena& scratchArena, const Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena>& edges, Vector<u32, Core::Alloc::ScratchArena>& outLoop);
    [[nodiscard]] static Expected<SIMDVectorSingleResult> CapNormal(SIMDVectorSingle area)noexcept;
    [[nodiscard]] static Expected<u32> FillCapLoop(
        const SIMDVectorSingle loopNormalVec,
        const Vector<u32, Core::Alloc::ScratchArena>& loop,
        CsgDeformVertexVector<Core::Alloc::ScratchArena>& inOutVertices,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& inOutTriangles
    );
    [[nodiscard]] static Expected<u32> FillCutCaps(
        Core::Alloc::ScratchArena& scratchArena,
        Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena>& scratchEdges,
        CsgDeformVertexVector<Core::Alloc::ScratchArena>& inOutVertices,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& inOutTriangles
    );


public:
    CsgDeformCapBuilder() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

