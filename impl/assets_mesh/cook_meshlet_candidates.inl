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
[[nodiscard]] static bool FindBestDisconnectedMeshletCandidate(
    const CookEntryT& entry,
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchOffset,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    MeshletFrontierCandidate& outCandidate
){
    bool found = false;
    for(usize triangleIndex = searchOffset; triangleIndex < trianglePrecompute.triangles.size(); ++triangleIndex){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

