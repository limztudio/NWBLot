// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_meshlets.h"

#include <impl/assets_mesh/cook_meshlet_scoring.adhd.h>
#include <impl/assets_mesh/cook_meshlet_metrics.adhd.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshCookMeshlets::GrowMeshletFromFrontier(
    const AStringView metaKind,
    const AStringView filePathText,
    const usize seedSearchOffset,
    Core::CpuTaskScheduler& cpuScheduler,
    MeshletTrianglePrecompute& trianglePrecompute,
    Core::Assets::AssetVector<u32>& localSourceVertexRefs,
    Core::Assets::AssetVector<u32>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena>& parallelCandidates,
    Core::Assets::AssetVector<u32>& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
){
    while(MeshletPrimitiveCount(meshlet) < s_MeshMaxMeshletTriangles){
        if(frontier.empty()){
            const auto disconnectedCandidate = MeshletCandidateSearch::FindBestDisconnectedMeshletCandidate(trianglePrecompute, seedSearchOffset + 1u, localTriangleIndices, scoreState, meshlet, localSourceVertexRefs, cpuScheduler, parallelCandidates);
            if(!disconnectedCandidate)
                break;

            if(!AddVisitedMeshletTriangle(
                metaKind,
                filePathText,
                disconnectedCandidate->triangleIndex,
                trianglePrecompute.triangles,
                trianglePrecompute.triangleCalculations,
                trianglePrecompute.positionTriangleOffsets,
                trianglePrecompute.positionTriangleIndices,
                trianglePrecompute.visitedTriangles,
                localSourceVertexRefs,
                localTriangleIndices,
                meshlet,
                primitiveIndices,
                scoreState,
                frontier,
                frontierFlags
            ))
                return false;
            continue;
        }

        const auto bestCandidate = FindBestMeshletFrontierCandidateFromPrecompute(
            trianglePrecompute.triangles,
            trianglePrecompute.triangleCalculations,
            frontier,
            localTriangleIndices,
            scoreState,
            meshlet,
            localSourceVertexRefs
        );
        if(!bestCandidate)
            break;

        RemoveMeshletFrontierCandidate(bestCandidate->frontierOffset, frontier, frontierFlags);
        if(!AddVisitedMeshletTriangle(
            metaKind,
            filePathText,
            bestCandidate->triangleIndex,
            trianglePrecompute.triangles,
            trianglePrecompute.triangleCalculations,
            trianglePrecompute.positionTriangleOffsets,
            trianglePrecompute.positionTriangleIndices,
            trianglePrecompute.visitedTriangles,
            localSourceVertexRefs,
            localTriangleIndices,
            meshlet,
            primitiveIndices,
            scoreState,
            frontier,
            frontierFlags
        ))
            return false;
    }

    return true;
}


b8 MeshCookMeshlets::BuildMeshlets(const Path& nwbFilePath, const TStringView metaKind, const Span<const u32> indices, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, MeshCookEntry& entry){
    auto precomputed = MeshletTriangleGeometry::PrecomputeMeshletTriangleData(nwbFilePath, metaKind, indices, entry, scratchArena);
    if(!precomputed)
        return false;
    MeshletTrianglePrecompute& trianglePrecompute = *precomputed;

    MeshletDesc current;
    const AString<Core::Alloc::ScratchArena> filePathText = PathToString<char>(scratchArena, nwbFilePath);
    Core::Assets::AssetArena& assetArena = entry.positions.get_allocator().arena();
    Core::Assets::AssetVector<u32> localSourceVertexRefs(assetArena);
    Core::Assets::AssetVector<u32> localTriangleIndices(assetArena);
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena> parallelCandidates(scratchArena);
    Core::Assets::AssetVector<u32> frontier(assetArena);
    Core::Assets::AssetVector<u8> frontierFlags(assetArena);
    PrepareMeshletBuildOutputStreams(
        indices.size(),
        trianglePrecompute.triangles.size(),
        entry.meshlets,
        entry.meshletBounds,
        entry.meshletPositionStreamRefs,
        entry.meshletAttributeStreamRefs,
        entry.meshletLocalVertexRefs,
        entry.meshletPrimitiveIndices,
        localSourceVertexRefs,
        localTriangleIndices,
        frontier,
        frontierFlags
    );
    parallelCandidates.reserve((static_cast<usize>(cpuScheduler.workerThreadCount()) + 1u) * s_MeshletDisconnectedCandidateParallelOversubscription);

    MeshletScoreState scoreState;

    auto resetCurrentMeshlet = [&]()noexcept{
        ResetMeshletBuilderState(entry.meshletPrimitiveIndices.size(), current, localSourceVertexRefs, localTriangleIndices, scoreState);
    };

    auto flushMeshlet = [&]() -> b8{
        if(!FlushBuiltMeshlet(
            s_MeshMetaKindText,
            AStringView(filePathText),
            entry.positions,
            entry.vertexRefs,
            entry.meshletPrimitiveIndices,
            localSourceVertexRefs,
            current,
            entry.meshletPositionStreamRefs,
            entry.meshletAttributeStreamRefs,
            entry.meshletLocalVertexRefs,
            entry.meshlets,
            entry.meshletBounds
        ))
            return false;

        resetCurrentMeshlet();
        return true;
    };

    usize seedSearchOffset = 0u;
    while(const Expected<u32> seedTriangleIndex = TryFindNextUnvisitedMeshletTriangle(trianglePrecompute.visitedTriangles, trianglePrecompute.triangles.size(), seedSearchOffset)){
        seedSearchOffset = *seedTriangleIndex;
        ClearMeshletFrontier(frontier, frontierFlags);
        resetCurrentMeshlet();

        if(!AddVisitedMeshletTriangle(
            s_MeshMetaKindText,
            AStringView(filePathText),
            *seedTriangleIndex,
            trianglePrecompute.triangles,
            trianglePrecompute.triangleCalculations,
            trianglePrecompute.positionTriangleOffsets,
            trianglePrecompute.positionTriangleIndices,
            trianglePrecompute.visitedTriangles,
            localSourceVertexRefs,
            localTriangleIndices,
            current,
            entry.meshletPrimitiveIndices,
            scoreState,
            frontier,
            frontierFlags
        ))
            return false;

        if(!GrowMeshletFromFrontier(s_MeshMetaKindText, AStringView(filePathText), seedSearchOffset, cpuScheduler, trianglePrecompute, localSourceVertexRefs, localTriangleIndices, current, entry.meshletPrimitiveIndices, scoreState, parallelCandidates, frontier, frontierFlags))
            return false;

        ClearMeshletFrontier(frontier, frontierFlags);
        if(!flushMeshlet())
            return false;
    }

    if(entry.meshlets.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet build produced no meshlets"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }

    LogMeshletCookMetrics(
        s_MeshMetaKindText,
        AStringView(filePathText),
        indices,
        entry.positions,
        entry.normals,
        entry.tangents,
        entry.uv0,
        entry.colors,
        entry.vertexRefs,
        entry.meshlets,
        entry.meshletBounds,
        entry.meshletPositionStreamRefs,
        entry.meshletAttributeStreamRefs,
        entry.meshletLocalVertexRefs,
        entry.meshletPrimitiveIndices
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

