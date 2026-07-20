template<typename FrontierVectorT>
static void ClearMeshletFrontier(FrontierVectorT& frontier, Core::Assets::AssetVector<u8>& frontierFlags){
    for(const u32 triangleIndex : frontier)
        frontierFlags[triangleIndex] = 0u;
    frontier.clear();
}

template<typename FrontierVectorT>
static void RemoveMeshletFrontierCandidate(
    FrontierVectorT& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags,
    const usize frontierOffset
){
    const u32 triangleIndex = frontier[frontierOffset];
    frontierFlags[triangleIndex] = 0u;
    frontier[frontierOffset] = frontier.back();
    frontier.pop_back();
}

template<typename FrontierVectorT>
static void AddMeshletTriangleNeighborsToFrontier(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const u32 triangleIndex,
    FrontierVectorT& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    for(const u32 positionIndex : triangle.positions){
        const u32 triangleOffsetBegin = trianglePrecompute.positionTriangleOffsets[positionIndex];
        const u32 triangleOffsetEnd = trianglePrecompute.positionTriangleOffsets[positionIndex + 1u];
        for(u32 triangleOffset = triangleOffsetBegin; triangleOffset < triangleOffsetEnd; ++triangleOffset){
            const u32 neighborTriangleIndex = trianglePrecompute.positionTriangleIndices[triangleOffset];
            if(
                neighborTriangleIndex == triangleIndex
                || trianglePrecompute.visitedTriangles[neighborTriangleIndex] != 0u
                || frontierFlags[neighborTriangleIndex] != 0u
            )
                continue;

            frontierFlags[neighborTriangleIndex] = 1u;
            frontier.push_back(neighborTriangleIndex);
        }
    }
}

template<typename VertexRefVectorT, typename PrimitiveIndexVectorT>
[[nodiscard]] static bool AddMeshletTriangleToBuilder(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const MeshletTriangleData& triangle,
    VertexRefVectorT& localSourceVertexRefs,
    MeshletDesc& meshlet,
    PrimitiveIndexVectorT& primitiveIndices
){
    for(const u32 vertexRefIndex : triangle.vertexRefs){
        u8 localVertex = 0u;
        const bool found = FindMeshletLocalVertex(localSourceVertexRefs, vertexRefIndex, localVertex);
        if(!found){
            if(localSourceVertexRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': triangle cannot fit within one meshlet")
                    , metaKind
                    , PathToString<tchar>(nwbFilePath)
                );
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

template<typename TriangleIndexVectorT, typename TriangleAreaNormalAtT>
static void AddMeshletTriangleToScoreState(
    const TriangleIndexVectorT& triangleIndices,
    const MeshletTriangleVectors& triangleVectors,
    const TriangleAreaNormalAtT& triangleAreaNormalAt,
    MeshletScoreState& state
){
    AccumulateMeshletScoreBounds(triangleVectors.positions, state.minBounds, state.maxBounds);
    state.radius = AabbTests::Radius(state.minBounds, state.maxBounds);
    state.centroidSum = VectorAdd(state.centroidSum, triangleVectors.centroid);
    state.normalSum = VectorAdd(state.normalSum, triangleVectors.areaNormal);
    state.normalAxis = NormalizeMeshletDirectionOrZero(state.normalSum);
    ++state.primitiveCount;
    state.coneCutoff = ComputeMeshletScoreConeCutoff(
        triangleIndices,
        state.normalAxis,
        0u,
        false,
        triangleAreaNormalAt,
        state.coneEnabled
    );
}

[[nodiscard]] static bool AddVisitedMeshletTriangle(
    const Path& nwbFilePath,
    const tchar* metaKind,
    MeshletTrianglePrecompute& trianglePrecompute,
    const u32 triangleIndex,
    Core::Assets::AssetVector<u32>& localSourceVertexRefs,
    Core::Assets::AssetVector<u32>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Core::Assets::AssetVector<u32>& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    if(!AddMeshletTriangleToBuilder(nwbFilePath, metaKind, triangle, localSourceVertexRefs, meshlet, primitiveIndices))
        return false;

    localTriangleIndices.push_back(triangleIndex);
    const MeshletTriangleVectors triangleVectors = MakeMeshletTriangleVectors(
        LoadFloat(triangle.positionVectors[0u]),
        LoadFloat(triangle.positionVectors[1u]),
        LoadFloat(triangle.positionVectors[2u]),
        LoadFloat(triangle.centroid),
        LoadFloat(triangle.areaNormal)
    );
    const auto triangleAreaNormalAt = [&](const u32 otherTriangleIndex){
        return LoadFloat(trianglePrecompute.triangles[otherTriangleIndex].areaNormal);
    };
    AddMeshletTriangleToScoreState(localTriangleIndices, triangleVectors, triangleAreaNormalAt, scoreState);
    trianglePrecompute.visitedTriangles[triangleIndex] = 1u;
    AddMeshletTriangleNeighborsToFrontier(trianglePrecompute, triangleIndex, frontier, frontierFlags);
    return true;
}

[[nodiscard]] static bool GrowMeshletFromFrontier(
    const Path& nwbFilePath,
    const tchar* metaKind,
    MeshletTrianglePrecompute& trianglePrecompute,
    const usize seedSearchOffset,
    Core::Assets::AssetVector<u32>& localSourceVertexRefs,
    Core::Assets::AssetVector<u32>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Core::Alloc::ThreadPool& threadPool,
    Core::Assets::AssetVector<MeshletCandidateSearchResult>& parallelCandidates,
    Core::Assets::AssetVector<u32>& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
){
    while(MeshletPrimitiveCount(meshlet) < s_MeshMaxMeshletTriangles){
        if(frontier.empty()){
            MeshletFrontierCandidate disconnectedCandidate;
            if(!FindBestDisconnectedMeshletCandidate(
                trianglePrecompute,
                seedSearchOffset + 1u,
                localTriangleIndices,
                scoreState,
                meshlet,
                localSourceVertexRefs,
                threadPool,
                parallelCandidates,
                disconnectedCandidate
            ))
                break;

            if(!AddVisitedMeshletTriangle(
                nwbFilePath,
                metaKind,
                trianglePrecompute,
                disconnectedCandidate.triangleIndex,
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

        MeshletFrontierCandidate bestCandidate;
        if(!FindBestMeshletFrontierCandidate(
            trianglePrecompute,
            frontier,
            localTriangleIndices,
            scoreState,
            meshlet,
            localSourceVertexRefs,
            bestCandidate
        ))
            break;

        RemoveMeshletFrontierCandidate(frontier, frontierFlags, bestCandidate.frontierOffset);
        if(!AddVisitedMeshletTriangle(
            nwbFilePath,
            metaKind,
            trianglePrecompute,
            bestCandidate.triangleIndex,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

