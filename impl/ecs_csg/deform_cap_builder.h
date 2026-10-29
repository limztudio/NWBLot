// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_wall_builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns cut boundary loop collection, deterministic ordering, and fan fills with one cap orientation rule, so preview and commit caps always agree.
class CsgDeformCapBuilder final : NoCopy{
public:
    // SIMD-domain cores: inputs and outputs stay on vector lanes, never touch storage.
    [[nodiscard]] static SIMDVectorSingle AccumulateFanAreaVec(SIMDVectorSingle inAreaVec, SIMDVectorSingle originVec, SIMDVectorSingle firstVec, SIMDVectorSingle secondVec)noexcept;
    static void CollectBoundaryEdges(Core::Alloc::ScratchArena& scratchArena, Span<const CsgDeformTriangle> triangles, Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena>& outEdges);
    [[nodiscard]] static bool OrderBoundaryLoop(Core::Alloc::ScratchArena& scratchArena, Span<const CsgDeformCutLoopEdge> edges, Vector<u32, Core::Alloc::ScratchArena>& outLoop);
    [[nodiscard]] static Expected<SIMDVectorSingleResult> CapNormal(SIMDVectorSingle areaVec)noexcept;
    [[nodiscard]] static Expected<u32> FillCapLoop(
        const SIMDVectorSingle loopNormalVec,
        Span<const u32> loop,
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
