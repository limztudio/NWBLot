// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_meshlets.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MeshCookMeshlets::ClearMeshletFrontier(Vector<u32, Core::Alloc::ScratchArena>& frontier, const Span<u8> frontierFlags)noexcept{
    for(const u32 triangleIndex : frontier)
        frontierFlags[triangleIndex] = 0u;
    frontier.clear();
}

void MeshCookMeshlets::RemoveMeshletFrontierCandidate(const usize frontierOffset, Vector<u32, Core::Alloc::ScratchArena>& frontier, const Span<u8> frontierFlags)noexcept{
    const u32 triangleIndex = frontier[frontierOffset];
    frontierFlags[triangleIndex] = 0u;
    frontier[frontierOffset] = frontier.back();
    frontier.pop_back();
}

void MeshCookMeshlets::AddMeshletTriangleNeighborsToFrontier(const MeshletTrianglePrecompute& trianglePrecompute, const u32 triangleIndex, Vector<u32, Core::Alloc::ScratchArena>& frontier, const Span<u8> frontierFlags){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    for(const u32 positionIndex : triangle.positions){
        const u32 triangleOffsetBegin = trianglePrecompute.positionTriangleOffsets[positionIndex];
        const u32 triangleOffsetEnd = trianglePrecompute.positionTriangleOffsets[positionIndex + 1u];
        for(u32 triangleOffset = triangleOffsetBegin; triangleOffset < triangleOffsetEnd; ++triangleOffset){
            const u32 neighborTriangleIndex = trianglePrecompute.positionTriangleIndices[triangleOffset];
            if(neighborTriangleIndex == triangleIndex || trianglePrecompute.visitedTriangles[neighborTriangleIndex] != 0u || frontierFlags[neighborTriangleIndex] != 0u)
                continue;

            frontierFlags[neighborTriangleIndex] = 1u;
            frontier.push_back(neighborTriangleIndex);
        }
    }
}

b8 MeshCookMeshlets::AddMeshletTriangleToBuilder(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const MeshletTriangleData& triangle,
    Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices
){
    for(const u32 vertexRefIndex : triangle.vertexRefs){
        u8 localVertex = 0u;
        if(const Expected<u8> foundVertex = TryFindMeshletLocalVertex(localSourceVertexRefs, vertexRefIndex))
            localVertex = *foundVertex;
        else{
            if(localSourceVertexRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': triangle cannot fit within one meshlet"), metaKind, PathToString<tchar>(nwbFilePath));
                return false;
            }
            localVertex = static_cast<u8>(localSourceVertexRefs.size());
            localSourceVertexRefs.push_back(vertexRefIndex);
        }
        primitiveIndices.push_back(localVertex);
    }
    meshlet.counts = PackMeshletCounts(0u, MeshletPrimitiveCount(meshlet) + 1u, 0u, 0u);
    return true;
}

b8 MeshCookMeshlets::AddVisitedMeshletTriangle(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const u32 triangleIndex,
    MeshletTrianglePrecompute& trianglePrecompute,
    Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
    Vector<u32, Core::Alloc::ScratchArena>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Vector<u32, Core::Alloc::ScratchArena>& frontier,
    const Span<u8> frontierFlags
){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    if(!AddMeshletTriangleToBuilder(nwbFilePath, metaKind, triangle, localSourceVertexRefs, meshlet, primitiveIndices))
        return false;

    localTriangleIndices.push_back(triangleIndex);
    AddMeshletTriangleToScoreStateFromCalculations(
        trianglePrecompute.triangleCalculations,
        localTriangleIndices,
        triangleIndex,
        scoreState
    );
    trianglePrecompute.visitedTriangles[triangleIndex] = 1u;
    AddMeshletTriangleNeighborsToFrontier(trianglePrecompute, triangleIndex, frontier, frontierFlags);
    return true;
}


b8 MeshCookMeshlets::GrowMeshletFromFrontier(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const usize seedSearchOffset,
    Core::CpuTaskScheduler& cpuScheduler,
    MeshletTrianglePrecompute& trianglePrecompute,
    Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
    Vector<u32, Core::Alloc::ScratchArena>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena>& parallelCandidates,
    Vector<u32, Core::Alloc::ScratchArena>& frontier,
    const Span<u8> frontierFlags
){
    while(MeshletPrimitiveCount(meshlet) < s_MeshMaxMeshletTriangles){
        if(frontier.empty()){
            const auto disconnectedCandidate = MeshletCandidateSearch::FindBestDisconnectedMeshletCandidate(trianglePrecompute, seedSearchOffset + 1u, localTriangleIndices, scoreState, meshlet, localSourceVertexRefs, cpuScheduler, parallelCandidates);
            if(!disconnectedCandidate)
                break;

            if(!AddVisitedMeshletTriangle(nwbFilePath, metaKind, disconnectedCandidate->triangleIndex, trianglePrecompute, localSourceVertexRefs, localTriangleIndices, meshlet, primitiveIndices, scoreState, frontier, frontierFlags))
                return false;
            continue;
        }

        const auto bestCandidate = MeshletCandidateSearch::FindBestMeshletFrontierCandidate(trianglePrecompute, frontier, localTriangleIndices, scoreState, meshlet, localSourceVertexRefs);
        if(!bestCandidate)
            break;

        RemoveMeshletFrontierCandidate(bestCandidate->frontierOffset, frontier, frontierFlags);
        if(!AddVisitedMeshletTriangle(nwbFilePath, metaKind, bestCandidate->triangleIndex, trianglePrecompute, localSourceVertexRefs, localTriangleIndices, meshlet, primitiveIndices, scoreState, frontier, frontierFlags))
            return false;
    }

    return true;
}


b8 MeshCookMeshlets::BuildMeshlets(const Path& nwbFilePath, const TStringView metaKind, const Span<const u32> indices, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, MeshCookEntry& entry){
    entry.meshlets.clear();
    entry.meshletBounds.clear();
    entry.meshletPositionStreamRefs.clear();
    entry.meshletAttributeStreamRefs.clear();
    entry.meshletLocalVertexRefs.clear();
    entry.meshletPrimitiveIndices.clear();
    entry.meshlets.reserve((indices.size() / s_MeshletTriangleIndexCount + s_MeshMaxMeshletTriangles - 1u) / s_MeshMaxMeshletTriangles);
    entry.meshletBounds.reserve(entry.meshlets.capacity());
    entry.meshletPositionStreamRefs.reserve(indices.size());
    entry.meshletAttributeStreamRefs.reserve(indices.size());
    entry.meshletLocalVertexRefs.reserve(indices.size());
    entry.meshletPrimitiveIndices.reserve(indices.size());

    auto precomputed = MeshletTriangleGeometry::PrecomputeMeshletTriangleData(nwbFilePath, metaKind, indices, entry, scratchArena);
    if(!precomputed)
        return false;
    MeshletTrianglePrecompute& trianglePrecompute = *precomputed;

    MeshletDesc current;
    const AString<Core::Alloc::ScratchArena> filePathText = PathToString<char>(scratchArena, nwbFilePath);
    Vector<u32, Core::Alloc::ScratchArena> localSourceVertexRefs(scratchArena);
    Vector<u32, Core::Alloc::ScratchArena> localTriangleIndices(scratchArena);
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena> parallelCandidates(scratchArena);
    Vector<u32, Core::Alloc::ScratchArena> frontier(scratchArena);
    Vector<u8, Core::Alloc::ScratchArena> frontierFlags(scratchArena);
    localSourceVertexRefs.reserve(s_MeshMaxMeshletVertices);
    localTriangleIndices.reserve(s_MeshMaxMeshletTriangles);
    parallelCandidates.reserve((static_cast<usize>(cpuScheduler.workerThreadCount()) + 1u) * s_MeshletDisconnectedCandidateParallelOversubscription);
    frontier.reserve(trianglePrecompute.triangles.size());
    frontierFlags.resize(trianglePrecompute.triangles.size(), 0u);

    MeshletScoreState scoreState;

    auto resetCurrentMeshlet = [&]()noexcept{
        current = MeshletDesc{};
        current.primitiveOffset = static_cast<u32>(entry.meshletPrimitiveIndices.size());
        localSourceVertexRefs.clear();
        localTriangleIndices.clear();
        MeshletCandidateSearch::ResetMeshletScoreState(scoreState);
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

        if(!AddVisitedMeshletTriangle(nwbFilePath, metaKind, *seedTriangleIndex, trianglePrecompute, localSourceVertexRefs, localTriangleIndices, current, entry.meshletPrimitiveIndices, scoreState, frontier, frontierFlags))
            return false;

        if(!GrowMeshletFromFrontier(nwbFilePath, metaKind, seedSearchOffset, cpuScheduler, trianglePrecompute, localSourceVertexRefs, localTriangleIndices, current, entry.meshletPrimitiveIndices, scoreState, parallelCandidates, frontier, frontierFlags))
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

