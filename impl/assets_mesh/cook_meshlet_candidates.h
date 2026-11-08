// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_meshlet_triangles.h"
#include <impl/assets_mesh/cook_types.adhd.h>
#include <impl/assets_mesh/cook_meshlet_scoring.adhd.h>

#include <core/task/cpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scores frontier and disconnected triangles against the growing meshlet and picks the best fit.
namespace MeshletCandidateSearch{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<MeshletFrontierCandidate> FindBestDisconnectedMeshletCandidate(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchOffset,
    const Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const Span<const u32> localVertexRefs,
    Core::CpuTaskScheduler& cpuScheduler,
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena>& parallelCandidates
){
    const usize triangleCount = trianglePrecompute.triangles.size();
    if(searchOffset >= triangleCount)
        return MakeUnexpected(Failure{});

    const usize searchCount = triangleCount - searchOffset;
    if(!cpuScheduler.isParallelEnabled() || searchCount < s_MeshletDisconnectedCandidateParallelThreshold){
        return FindBestDisconnectedMeshletCandidateRangeFromPrecompute(
            trianglePrecompute.triangles,
            trianglePrecompute.triangleCalculations,
            trianglePrecompute.visitedTriangles,
            searchOffset,
            triangleCount,
            triangleIndices,
            scoreState,
            meshlet,
            localVertexRefs
        );
    }

    const usize chunkCount = ResolveMeshletDisconnectedCandidateChunkCount(searchCount, cpuScheduler.workerThreadCount());

    parallelCandidates.clear();
    parallelCandidates.resize(chunkCount);
    const auto parallelWork = cpuScheduler.parallelFor(static_cast<usize>(0), chunkCount, [&](const usize chunkIndex)noexcept(
        noexcept(FindBestDisconnectedMeshletCandidateRangeFromPrecompute(trianglePrecompute.triangles, trianglePrecompute.triangleCalculations, trianglePrecompute.visitedTriangles, DeclVal<const usize&>(), DeclVal<const usize&>(), triangleIndices, scoreState, meshlet, localVertexRefs))
    ){
        usize chunkBegin = 0u;
        usize chunkEnd = 0u;
        ResolveMeshletDisconnectedCandidateChunkRange(searchOffset, searchCount, chunkCount, chunkIndex, chunkBegin, chunkEnd);

        MeshletCandidateSearchResult result;
        const auto foundCandidate = FindBestDisconnectedMeshletCandidateRangeFromPrecompute(
            trianglePrecompute.triangles,
            trianglePrecompute.triangleCalculations,
            trianglePrecompute.visitedTriangles,
            chunkBegin,
            chunkEnd,
            triangleIndices,
            scoreState,
            meshlet,
            localVertexRefs
        );
        result.found = foundCandidate.has_value();
        if(foundCandidate)
            result.candidate = *foundCandidate;
        parallelCandidates[chunkIndex] = result;
    });
    if(!parallelWork){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshCook: disconnected candidate preparation rejected CPU work: {}"), static_cast<u32>(parallelWork.error()));
        return MakeUnexpected(Failure{});
    }

    return SelectBestMeshletCandidateFromResults(parallelCandidates);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

