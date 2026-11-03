// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_edit.h"

#include "deform_cap_builder.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgDeformViability CheckCsgDeformCutsViability(
    Core::Alloc::ScratchArena& scratchArena,
    const Span<const CsgDeformVertex> inputVertices,
    const Span<const CsgDeformTriangle> inputTriangles,
    const Span<const CsgDeformCutDesc> cuts,
    const CsgDeformBuildOptions& options,
    const CsgShapeRegistry* shapeRegistry
){
    CsgDeformVertexVector<Core::Alloc::ScratchArena> vertices(scratchArena);
    CsgDeformTriangleVector<Core::Alloc::ScratchArena> triangles(scratchArena);
    const auto result = PreviewCsgDeformCuts(scratchArena, inputVertices, inputTriangles, cuts, options, shapeRegistry, vertices, triangles);
    return result ? MakeCsgDeformViability(true, CsgDeformViabilityReason::Ok) : MakeCsgDeformViability(false, result.error().reason);
}

Expected<CsgDeformStats, CsgDeformFailure> PreviewCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    const Span<const CsgDeformVertex> inputVertices,
    const Span<const CsgDeformTriangle> inputTriangles,
    const Span<const CsgDeformCutDesc> cuts,
    const CsgDeformBuildOptions& options,
    const CsgShapeRegistry* shapeRegistry,
    CsgDeformVertexVector<Core::Alloc::ScratchArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& outTriangles
){
    const usize inputVertexCount = inputVertices.size();
    const usize inputTriangleCount = inputTriangles.size();
    const usize cutCount = cuts.size();
    CsgDeformStats stats;
    outVertices.clear();
    outTriangles.clear();
    stats.inputVertexCount = static_cast<u32>(inputVertexCount);
    stats.inputTriangleCount = static_cast<u32>(inputTriangleCount);
    const auto boundaryEpsilonResult = TryValidateCsgDeformPreviewInputs(inputVertices, inputTriangles, options);
    if(!boundaryEpsilonResult)
        return MakeUnexpected(MakeCsgDeformFailure(boundaryEpsilonResult.error(), stats));

    const f32 boundaryEpsilon = *boundaryEpsilonResult;
    outVertices.reserve(inputVertexCount + cutCount * s_RebuildReservePerCut);
    outTriangles.reserve(inputTriangleCount * s_KeptReserveMultiplier + cutCount * s_RebuildReservePerCut);
    AssignVector(outVertices, inputVertices);
    AssignVector(outTriangles, inputTriangles);

    CsgDeformTriangleVector<Core::Alloc::ScratchArena> scratchKept(scratchArena);
    Vector<f32, Core::Alloc::ScratchArena> scratchDistances(scratchArena);
    Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena> scratchEdges(scratchArena);

    u32 appliedCuts = 0u;
    u32 capTriangles = 0u;
    for(const CsgDeformCutDesc& cut : cuts){
        if(!cut.active)
            continue;
        const auto clipped = CsgDeformWallBuilder::ClipShell(scratchArena, cut.shape, boundaryEpsilon, shapeRegistry, scratchKept, scratchDistances, outVertices, outTriangles);
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
    Core::Alloc::ScratchArena& scratchArena,
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
    CsgDeformVertexVector<Core::Alloc::ScratchArena> previewVertices(scratchArena);
    CsgDeformTriangleVector<Core::Alloc::ScratchArena> previewTriangles(scratchArena);
    const auto preview = PreviewCsgDeformCuts(scratchArena, inputVertices, inputTriangles, cuts, options, shapeRegistry, previewVertices, previewTriangles);
    if(!preview)
        return MakeUnexpected(preview.error());
    AssignVector(outVertices, previewVertices);
    AssignVector(outTriangles, previewTriangles);
    return *preview;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

