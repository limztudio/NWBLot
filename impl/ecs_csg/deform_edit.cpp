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
    CsgDeformPipelineResult result;
    if(!CsgDeformPipeline::RebuildSequentialCuts(
        scratchArena,
        inputVertices,
        inputVertexCount,
        inputTriangles,
        inputTriangleCount,
        cuts,
        cutCount,
        options,
        vertices,
        triangles,
        result
    ))
        return result.viability;
    return result.viability;
}

bool PreviewCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    const usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<Core::Alloc::ScratchArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& outTriangles,
    CsgDeformStats& outStats
){
    outStats = CsgDeformStats{};
    CsgDeformPipelineResult result;
    if(!CsgDeformPipeline::RebuildSequentialCuts(
        scratchArena,
        inputVertices,
        inputVertexCount,
        inputTriangles,
        inputTriangleCount,
        cuts,
        cutCount,
        options,
        outVertices,
        outTriangles,
        result
    )){
        outStats = result.stats;
        return false;
    }
    outStats = result.stats;
    return result.viability.viable;
}

bool CommitCsgDeformCuts(
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
    CsgDeformTriangleVector<Core::Alloc::GlobalArena>& outTriangles,
    CsgDeformStats& outStats
){
    static_cast<void>(commitArena);
    outStats = CsgDeformStats{};
    outVertices.clear();
    outTriangles.clear();
    // Commit reuses the preview entry point so both always observe the same rebuild, viability classifier, and stats for identical inputs.
    CsgDeformVertexVector<Core::Alloc::ScratchArena> previewVertices(scratchArena);
    CsgDeformTriangleVector<Core::Alloc::ScratchArena> previewTriangles(scratchArena);
    CsgDeformStats previewStats{};
    if(!PreviewCsgDeformCuts(
        scratchArena,
        inputVertices,
        inputVertexCount,
        inputTriangles,
        inputTriangleCount,
        cuts,
        cutCount,
        options,
        previewVertices,
        previewTriangles,
        previewStats
    )){
        outStats = previewStats;
        return false;
    }
    outVertices.reserve(previewVertices.size());
    outTriangles.reserve(previewTriangles.size());
    for(const CsgDeformVertex& vertex : previewVertices)
        outVertices.push_back(vertex);
    for(const CsgDeformTriangle& triangle : previewTriangles)
        outTriangles.push_back(triangle);
    outStats = previewStats;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

