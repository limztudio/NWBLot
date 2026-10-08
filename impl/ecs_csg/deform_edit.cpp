// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_edit.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgDeformViability CheckCsgDeformCutsViability(
    Core::Alloc::ScratchArena& scratchArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    const usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options
){
    CsgDeformVertexVector<Core::Alloc::ScratchArena> vertices(scratchArena);
    CsgDeformTriangleVector<Core::Alloc::ScratchArena> triangles(scratchArena);
    const auto result = CsgDeformPipeline::RebuildSequentialCuts(
        scratchArena,
        inputVertices,
        inputVertexCount,
        inputTriangles,
        inputTriangleCount,
        cuts,
        cutCount,
        options,
        vertices,
        triangles
    );
    return result
        ? CsgDeformViability{ true, CsgDeformViabilityReason::Ok }
        : CsgDeformViability{ false, result.error().reason }
    ;
}

Expected<CsgDeformStats, CsgDeformFailure> PreviewCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    const usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<Core::Alloc::ScratchArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& outTriangles
){
    return CsgDeformPipeline::RebuildSequentialCuts(
        scratchArena,
        inputVertices,
        inputVertexCount,
        inputTriangles,
        inputTriangleCount,
        cuts,
        cutCount,
        options,
        outVertices,
        outTriangles
    );
}

Expected<CsgDeformStats, CsgDeformFailure> CommitCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    Core::Alloc::GlobalArena& commitArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    const usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<Core::Alloc::GlobalArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::GlobalArena>& outTriangles
){
    if(outVertices.get_allocator().arenaPtr() != &commitArena || outTriangles.get_allocator().arenaPtr() != &commitArena)
        return MakeUnexpected(CsgDeformFailure{ CsgDeformViabilityReason::InvalidOutputArena, {} });
    outVertices.clear();
    outTriangles.clear();
    // Commit reuses the preview entry point so both always observe the same rebuild, viability classifier, and stats for identical inputs.
    CsgDeformVertexVector<Core::Alloc::ScratchArena> previewVertices(scratchArena);
    CsgDeformTriangleVector<Core::Alloc::ScratchArena> previewTriangles(scratchArena);
    const auto preview = PreviewCsgDeformCuts(
        scratchArena,
        inputVertices,
        inputVertexCount,
        inputTriangles,
        inputTriangleCount,
        cuts,
        cutCount,
        options,
        previewVertices,
        previewTriangles
    );
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

