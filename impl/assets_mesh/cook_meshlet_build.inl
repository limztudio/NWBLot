
template<typename CookEntryT>
static bool BuildMeshlets(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    CookEntryT& entry,
    Core::Alloc::ThreadPool& threadPool
){
    entry.meshlets.clear();
    entry.meshletBounds.clear();
    entry.meshletPositionStreamRefs.clear();
    entry.meshletAttributeStreamRefs.clear();
    entry.meshletLocalVertexRefs.clear();
    entry.meshletPrimitiveIndices.clear();
    entry.meshlets.reserve((indices.size() / 3u + s_MeshMaxMeshletTriangles - 1u) / s_MeshMaxMeshletTriangles);
    entry.meshletBounds.reserve(entry.meshlets.capacity());
    entry.meshletPositionStreamRefs.reserve(indices.size());
    entry.meshletAttributeStreamRefs.reserve(indices.size());
    entry.meshletLocalVertexRefs.reserve(indices.size());
    entry.meshletPrimitiveIndices.reserve(indices.size());

    MeshletTrianglePrecompute trianglePrecompute(entry.positions.get_allocator().arena());
    if(!PrecomputeMeshletTriangleData(nwbFilePath, metaKind, indices, entry, trianglePrecompute))
        return false;

    MeshletDesc current;
    Core::Assets::AssetVector<u32> localSourceVertexRefs(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<MeshletPositionStreamRef> localPositionRefs(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<MeshletAttributeStreamRef> localAttributeRefs(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<u32> localAttributeSkins(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<MeshletLocalVertexRef> localVertexRefs(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<u32> localTriangleIndices(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<MeshletCandidateSearchResult> parallelCandidates(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<u32> frontier(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<u8> frontierFlags(entry.positions.get_allocator().arena());
    localSourceVertexRefs.reserve(s_MeshMaxMeshletVertices);
    localPositionRefs.reserve(s_MeshMaxMeshletVertices);
    localAttributeRefs.reserve(s_MeshMaxMeshletVertices);
    localAttributeSkins.reserve(s_MeshMaxMeshletVertices);
    localVertexRefs.reserve(s_MeshMaxMeshletVertices);
    localTriangleIndices.reserve(s_MeshMaxMeshletTriangles);
    frontier.reserve(s_MeshMaxMeshletTriangles * 3u);
    frontierFlags.resize(trianglePrecompute.triangles.size(), 0u);

    MeshletScoreState scoreState;

    auto resetCurrentMeshlet = [&](){
        current = MeshletDesc{};
        current.primitiveOffset = static_cast<u32>(entry.meshletPrimitiveIndices.size());
        localSourceVertexRefs.clear();
        localTriangleIndices.clear();
        ResetMeshletScoreState(scoreState);
    };

    auto flushMeshlet = [&]() -> bool{
        const u32 primitiveCount = MeshletPrimitiveCount(current);
        if(primitiveCount == 0u)
            return true;

        if(!BuildZippedMeshletRefs(
            nwbFilePath,
            metaKind,
            entry,
            localSourceVertexRefs,
            localPositionRefs,
            localAttributeRefs,
            localAttributeSkins,
            localVertexRefs
        ))
            return false;

        current.localVertexOffset = static_cast<u32>(entry.meshletLocalVertexRefs.size());
        current.positionRefOffset = static_cast<u32>(entry.meshletPositionStreamRefs.size());
        current.attributeRefOffset = static_cast<u32>(entry.meshletAttributeStreamRefs.size());
        current.counts = PackMeshletCounts(
            static_cast<u32>(localVertexRefs.size()),
            primitiveCount,
            static_cast<u32>(localPositionRefs.size()),
            static_cast<u32>(localAttributeRefs.size())
        );

        entry.meshletPositionStreamRefs.insert(entry.meshletPositionStreamRefs.end(), localPositionRefs.begin(), localPositionRefs.end());
        entry.meshletAttributeStreamRefs.insert(
            entry.meshletAttributeStreamRefs.end(),
            localAttributeRefs.begin(),
            localAttributeRefs.end()
        );
        entry.meshletLocalVertexRefs.insert(
            entry.meshletLocalVertexRefs.end(),
            localVertexRefs.begin(),
            localVertexRefs.end()
        );
        entry.meshlets.push_back(current);
        entry.meshletBounds.push_back(BuildMeshletBounds(entry, current));

        resetCurrentMeshlet();
        return true;
    };

    u32 seedTriangleIndex = 0u;
    usize seedSearchOffset = 0u;
    while(FindNextUnvisitedMeshletTriangle(trianglePrecompute, seedSearchOffset, seedTriangleIndex)){
        seedSearchOffset = seedTriangleIndex;
        ClearMeshletFrontier(frontier, frontierFlags);
        resetCurrentMeshlet();

        if(!AddVisitedMeshletTriangle(
            nwbFilePath,
            metaKind,
            trianglePrecompute,
            seedTriangleIndex,
            localSourceVertexRefs,
            localTriangleIndices,
            current,
            entry.meshletPrimitiveIndices,
            scoreState,
            frontier,
            frontierFlags
        ))
            return false;

        if(!GrowMeshletFromFrontier(
            nwbFilePath,
            metaKind,
            trianglePrecompute,
            seedSearchOffset,
            localSourceVertexRefs,
            localTriangleIndices,
            current,
            entry.meshletPrimitiveIndices,
            scoreState,
            threadPool,
            parallelCandidates,
            frontier,
            frontierFlags
        ))
            return false;

        ClearMeshletFrontier(frontier, frontierFlags);
        if(!flushMeshlet())
            return false;
    }

    if(entry.meshlets.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet build produced no meshlets")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    LogMeshletCookMetrics(nwbFilePath, metaKind, indices, entry);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

