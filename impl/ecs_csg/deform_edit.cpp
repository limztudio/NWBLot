// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_edit.h"

#include "deform_cap_builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = Core::Alloc::ScratchArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgDeformViability CheckCsgDeformCutsViability(
    ScratchArena& scratchArena,
    const Span<const CsgDeformVertex> inputVertices,
    const Span<const CsgDeformTriangle> inputTriangles,
    const Span<const CsgDeformCutDesc> cuts,
    const CsgDeformBuildOptions& options,
    const CsgShapeRegistry* shapeRegistry
){
    CsgDeformVertexVector<ScratchArena> vertices(scratchArena);
    CsgDeformTriangleVector<ScratchArena> triangles(scratchArena);
    const auto result = PreviewCsgDeformCuts(scratchArena, inputVertices, inputTriangles, cuts, options, shapeRegistry, vertices, triangles);
    return result ? CsgDeformViability{ true, CsgDeformViabilityReason::Ok } : CsgDeformViability{ false, result.error().reason };
}

Expected<CsgDeformStats, CsgDeformFailure> PreviewCsgDeformCuts(
    ScratchArena& scratchArena,
    const Span<const CsgDeformVertex> inputVertices,
    const Span<const CsgDeformTriangle> inputTriangles,
    const Span<const CsgDeformCutDesc> cuts,
    const CsgDeformBuildOptions& options,
    const CsgShapeRegistry* shapeRegistry,
    CsgDeformVertexVector<ScratchArena>& outVertices,
    CsgDeformTriangleVector<ScratchArena>& outTriangles
){
    const usize inputVertexCount = inputVertices.size();
    const usize inputTriangleCount = inputTriangles.size();
    const usize cutCount = cuts.size();
    CsgDeformStats stats;
    outVertices.clear();
    outTriangles.clear();
    stats.inputVertexCount = static_cast<u32>(inputVertexCount);
    stats.inputTriangleCount = static_cast<u32>(inputTriangleCount);
    if(inputVertexCount == 0u || inputTriangleCount == 0u)
        return MakeUnexpected(MakeCsgDeformFailure(CsgDeformViabilityReason::EmptyInput, stats));
    if(inputVertexCount > s_MaxDeformVertices || inputTriangleCount > s_MaxDeformTriangles)
        return MakeUnexpected(MakeCsgDeformFailure(CsgDeformViabilityReason::TooLarge, stats));
    if(!CsgDeformValidOptions(options))
        return MakeUnexpected(MakeCsgDeformFailure(CsgDeformViabilityReason::InvalidCutter, stats));
    if(!IsFiniteCsgDeformVertices(inputVertices))
        return MakeUnexpected(MakeCsgDeformFailure(CsgDeformViabilityReason::NonFiniteInput, stats));
    if(!CsgDeformValidTopology(inputTriangles, inputVertexCount))
        return MakeUnexpected(MakeCsgDeformFailure(CsgDeformViabilityReason::InvalidTopology, stats));

    const SIMDVectorSingle epsilon = VectorReplicate(Max(options.distanceEpsilon, s_MinEpsilon));
    outVertices.reserve(inputVertexCount + cutCount * s_RebuildReservePerCut);
    outTriangles.reserve(inputTriangleCount * s_KeptReserveMultiplier + cutCount * s_RebuildReservePerCut);
    for(const CsgDeformVertex& vertex : inputVertices)
        outVertices.push_back(vertex);
    for(const CsgDeformTriangle& triangle : inputTriangles)
        outTriangles.push_back(triangle);

    CsgDeformTriangleVector<ScratchArena> scratchKept(scratchArena);
    Vector<f32, ScratchArena> scratchDistances(scratchArena);
    Vector<CsgDeformCutLoopEdge, ScratchArena> scratchEdges(scratchArena);

    u32 appliedCuts = 0u;
    u32 capTriangles = 0u;
    for(const CsgDeformCutDesc& cut : cuts){
        if(!cut.active)
            continue;
        const auto clipped = CsgDeformWallBuilder::ClipShell(scratchArena, cut.shape, epsilon, shapeRegistry, scratchKept, scratchDistances, outVertices, outTriangles);
        if(!clipped)
            return MakeUnexpected(MakeCsgDeformFailure(clipped.error(), stats));
        ++appliedCuts;
        if(outTriangles.empty() || outVertices.size() > s_MaxDeformVertices || outTriangles.size() > s_MaxDeformTriangles){
            const auto reason = outTriangles.empty() ? CsgDeformViabilityReason::NoKeptGeometry : CsgDeformViabilityReason::TooLarge;
            return MakeUnexpected(MakeCsgDeformFailure(reason, stats));
        }
        if(options.fillCaps){
            const auto cutCaps = CsgDeformCapBuilder::FillCutCaps(scratchArena, scratchEdges, outVertices, outTriangles);
            if(!cutCaps)
                return MakeUnexpected(MakeCsgDeformFailure(CsgDeformViabilityReason::CapLoopFailed, stats));
            capTriangles += *cutCaps;
        }
    }

    if(outVertices.empty() || outTriangles.empty())
        return MakeUnexpected(MakeCsgDeformFailure(CsgDeformViabilityReason::NoKeptGeometry, stats));
    if(!IsFiniteCsgDeformVertices(outVertices))
        return MakeUnexpected(MakeCsgDeformFailure(CsgDeformViabilityReason::NonFiniteInput, stats));
    stats.outputVertexCount = static_cast<u32>(outVertices.size());
    stats.outputTriangleCount = static_cast<u32>(outTriangles.size());
    stats.appliedCutCount = appliedCuts;
    stats.capTriangleCount = capTriangles;
    return stats;
}

Expected<CsgDeformStats, CsgDeformFailure> CommitCsgDeformCuts(
    ScratchArena& scratchArena,
    Core::Alloc::GlobalArena& commitArena,
    const Span<const CsgDeformVertex> inputVertices,
    const Span<const CsgDeformTriangle> inputTriangles,
    const Span<const CsgDeformCutDesc> cuts,
    const CsgDeformBuildOptions& options,
    const CsgShapeRegistry* shapeRegistry,
    CsgDeformVertexVector<Core::Alloc::GlobalArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::GlobalArena>& outTriangles
){
    if(outVertices.get_allocator().arenaPtr() != &commitArena || outTriangles.get_allocator().arenaPtr() != &commitArena)
        return MakeUnexpected(MakeCsgDeformFailure(CsgDeformViabilityReason::InvalidOutputArena, {}));
    outVertices.clear();
    outTriangles.clear();
    // Commit reuses the preview entry point so both always observe the same rebuild, viability classifier, and stats for identical inputs.
    CsgDeformVertexVector<ScratchArena> previewVertices(scratchArena);
    CsgDeformTriangleVector<ScratchArena> previewTriangles(scratchArena);
    const auto preview = PreviewCsgDeformCuts(scratchArena, inputVertices, inputTriangles, cuts, options, shapeRegistry, previewVertices, previewTriangles);
    if(!preview)
        return MakeUnexpected(preview.error());
    outVertices.reserve(previewVertices.size());
    outTriangles.reserve(previewTriangles.size());
    for(const CsgDeformVertex& vertex : previewVertices)
        outVertices.push_back(vertex);
    for(const CsgDeformTriangle& triangle : previewTriangles)
        outTriangles.push_back(triangle);
    return *preview;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


