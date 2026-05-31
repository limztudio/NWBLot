// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VertexRefVectorT>
[[nodiscard]] static bool MeshletCanFitTriangle(
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    const MeshletTriangleData& triangle,
    u32& outSharedVertexCount,
    u32& outMissingVertexCount
){
    if(MeshletPrimitiveCount(meshlet) + 1u > s_MeshMaxMeshletTriangles)
        return false;

    outMissingVertexCount = CountMeshletMissingVertices(localVertexRefs, triangle, outSharedVertexCount);
    return localVertexRefs.size() + outMissingVertexCount <= s_MeshMaxMeshletVertices;
}

template<typename CookEntryT, typename TriangleIndexVectorT, typename VertexRefVectorT>
static void UpdateBestMeshletCandidateIfBetter(
    const CookEntryT& entry,
    const MeshletTrianglePrecompute& trianglePrecompute,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    const usize frontierOffset,
    const u32 triangleIndex,
    const bool disconnected,
    bool& found,
    MeshletFrontierCandidate& outCandidate
){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    u32 sharedVertexCount = 0u;
    u32 missingVertexCount = 0u;
    if(!MeshletCanFitTriangle(meshlet, localVertexRefs, triangle, sharedVertexCount, missingVertexCount))
        return;

    const f32 score = ScoreMeshletCandidate(
        entry,
        trianglePrecompute,
        triangleIndices,
        scoreState,
        triangleIndex,
        sharedVertexCount,
        missingVertexCount,
        disconnected
    );
    if(found && (score < outCandidate.score || (score == outCandidate.score && triangleIndex > outCandidate.triangleIndex)))
        return;

    found = true;
    outCandidate.frontierOffset = frontierOffset;
    outCandidate.triangleIndex = triangleIndex;
    outCandidate.score = score;
}

static void UpdateBestMeshletCandidateFromResult(
    const MeshletCandidateSearchResult& candidate,
    bool& found,
    MeshletFrontierCandidate& outCandidate
){
    if(!candidate.found)
        return;

    if(
        found
        && (
            candidate.candidate.score < outCandidate.score
            || (candidate.candidate.score == outCandidate.score && candidate.candidate.triangleIndex > outCandidate.triangleIndex)
        )
    )
        return;

    found = true;
    outCandidate = candidate.candidate;
}

template<typename CookEntryT, typename TriangleIndexVectorT, typename VertexRefVectorT>
[[nodiscard]] static bool FindBestMeshletFrontierCandidate(
    const CookEntryT& entry,
    const MeshletTrianglePrecompute& trianglePrecompute,
    const Core::Assets::AssetVector<u32>& frontier,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    MeshletFrontierCandidate& outCandidate
){
    bool found = false;
    for(usize frontierOffset = 0u; frontierOffset < frontier.size(); ++frontierOffset){
        const u32 triangleIndex = frontier[frontierOffset];
        UpdateBestMeshletCandidateIfBetter(
            entry,
            trianglePrecompute,
            triangleIndices,
            scoreState,
            meshlet,
            localVertexRefs,
            frontierOffset,
            triangleIndex,
            false,
            found,
            outCandidate
        );
    }

    return found;
}

template<typename CookEntryT, typename TriangleIndexVectorT, typename VertexRefVectorT>
[[nodiscard]] static bool FindBestDisconnectedMeshletCandidateRange(
    const CookEntryT& entry,
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchBegin,
    const usize searchEnd,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    MeshletFrontierCandidate& outCandidate
){
    bool found = false;
    for(usize triangleIndex = searchBegin; triangleIndex < searchEnd; ++triangleIndex){
        if(trianglePrecompute.visitedTriangles[triangleIndex] != 0u)
            continue;

        UpdateBestMeshletCandidateIfBetter(
            entry,
            trianglePrecompute,
            triangleIndices,
            scoreState,
            meshlet,
            localVertexRefs,
            0u,
            static_cast<u32>(triangleIndex),
            true,
            found,
            outCandidate
        );
    }

    return found;
}

template<typename CookEntryT, typename TriangleIndexVectorT, typename VertexRefVectorT>
[[nodiscard]] static bool FindBestDisconnectedMeshletCandidate(
    const CookEntryT& entry,
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchOffset,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    Core::Alloc::ThreadPool& threadPool,
    Core::Assets::AssetVector<MeshletCandidateSearchResult>& parallelCandidates,
    MeshletFrontierCandidate& outCandidate
){
    const usize triangleCount = trianglePrecompute.triangles.size();
    if(searchOffset >= triangleCount)
        return false;

    const usize searchCount = triangleCount - searchOffset;
    if(!threadPool.isParallelEnabled() || searchCount < s_MeshletDisconnectedCandidateParallelThreshold){
        return FindBestDisconnectedMeshletCandidateRange(
            entry,
            trianglePrecompute,
            searchOffset,
            triangleCount,
            triangleIndices,
            scoreState,
            meshlet,
            localVertexRefs,
            outCandidate
        );
    }

    const usize workerCount = static_cast<usize>(threadPool.workerThreadCount()) + 1u;
    const usize maxChunkCount = workerCount * s_MeshletDisconnectedCandidateParallelOversubscription;
    const usize chunkCount = searchCount < maxChunkCount ? searchCount : maxChunkCount;
    const usize chunkSize = searchCount / chunkCount;
    const usize remainder = searchCount % chunkCount;

    parallelCandidates.clear();
    parallelCandidates.resize(chunkCount);
    threadPool.parallelFor(static_cast<usize>(0), chunkCount, [&](const usize chunkIndex){
        const usize chunkBegin = searchOffset + chunkIndex * chunkSize + (chunkIndex < remainder ? chunkIndex : remainder);
        const usize chunkEnd = chunkBegin + chunkSize + (chunkIndex < remainder ? 1u : 0u);

        MeshletCandidateSearchResult result;
        result.found = FindBestDisconnectedMeshletCandidateRange(
            entry,
            trianglePrecompute,
            chunkBegin,
            chunkEnd,
            triangleIndices,
            scoreState,
            meshlet,
            localVertexRefs,
            result.candidate
        );
        parallelCandidates[chunkIndex] = result;
    });

    bool found = false;
    for(const MeshletCandidateSearchResult& result : parallelCandidates)
        UpdateBestMeshletCandidateFromResult(result, found, outCandidate);

    return found;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

