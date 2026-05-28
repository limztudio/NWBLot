// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    for(const u32 vertexRefIndex : triangle.vertexRefs){
        const u32 triangleOffsetBegin = trianglePrecompute.vertexTriangleOffsets[vertexRefIndex];
        const u32 triangleOffsetEnd = trianglePrecompute.vertexTriangleOffsets[vertexRefIndex + 1u];
        for(u32 triangleOffset = triangleOffsetBegin; triangleOffset < triangleOffsetEnd; ++triangleOffset){
            const u32 neighborTriangleIndex = trianglePrecompute.vertexTriangleIndices[triangleOffset];
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

template<typename CookEntryT, typename TriangleIndexVectorT>
static void AddMeshletTriangleToScoreState(
    const CookEntryT& entry,
    const MeshletTrianglePrecompute& trianglePrecompute,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletTriangleData& triangle,
    MeshletScoreState& state
){
    AccumulateMeshletScoreBounds(entry, triangle, state.minBounds, state.maxBounds, state.hasGeometry);
    state.radius = ComputeMeshletScoreBoundsRadius(state.minBounds, state.maxBounds);
    state.centroidSum = VectorAdd(state.centroidSum, LoadFloat(triangle.centroid));
    state.normalSum = VectorAdd(state.normalSum, LoadFloat(triangle.areaNormal));
    state.normalAxis = NormalizeMeshletDirectionOrZero(state.normalSum);
    ++state.primitiveCount;
    state.coneCutoff = ComputeMeshletScoreConeCutoff(
        trianglePrecompute,
        triangleIndices,
        state.normalAxis,
        0u,
        false,
        state.coneEnabled
    );
}

template<typename CookEntryT>
[[nodiscard]] static bool AddVisitedMeshletTriangle(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const CookEntryT& entry,
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
    AddMeshletTriangleToScoreState(entry, trianglePrecompute, localTriangleIndices, triangle, scoreState);
    trianglePrecompute.visitedTriangles[triangleIndex] = 1u;
    AddMeshletTriangleNeighborsToFrontier(trianglePrecompute, triangleIndex, frontier, frontierFlags);
    return true;
}

template<typename CookEntryT>
[[nodiscard]] static bool GrowMeshletFromFrontier(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const CookEntryT& entry,
    MeshletTrianglePrecompute& trianglePrecompute,
    const usize seedSearchOffset,
    Core::Assets::AssetVector<u32>& localSourceVertexRefs,
    Core::Assets::AssetVector<u32>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Core::Assets::AssetVector<u32>& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
){
    while(MeshletPrimitiveCount(meshlet) < s_MeshMaxMeshletTriangles){
        if(frontier.empty()){
            MeshletFrontierCandidate disconnectedCandidate;
            if(!FindBestDisconnectedMeshletCandidate(
                entry,
                trianglePrecompute,
                seedSearchOffset + 1u,
                localTriangleIndices,
                scoreState,
                meshlet,
                localSourceVertexRefs,
                disconnectedCandidate
            ))
                break;

            if(!AddVisitedMeshletTriangle(
                nwbFilePath,
                metaKind,
                entry,
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
            entry,
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
            entry,
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

