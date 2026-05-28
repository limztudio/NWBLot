// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CookEntryT>
[[nodiscard]] static SIMDVector LoadMeshletSourceVertexPositionVector(const CookEntryT& entry, const u32 vertexRefIndex){
    const MeshVertexRef& vertexRef = entry.vertexRefs[vertexRefIndex];
    return VectorSetW(LoadFloat(entry.positions[vertexRef.position]), 0.0f);
}

template<typename CookEntryT>
[[nodiscard]] static bool PrecomputeMeshletTriangleData(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const CookEntryT& entry,
    MeshletTrianglePrecompute& outData
){
    outData.triangles.clear();
    outData.vertexTriangleOffsets.clear();
    outData.vertexTriangleIndices.clear();
    outData.visitedTriangles.clear();

    const usize triangleCount = indices.size() / 3u;
    if(triangleCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet triangle count exceeds u32 limits")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outData.triangles.resize(triangleCount);
    outData.vertexTriangleOffsets.resize(entry.vertexRefs.size() + 1u, 0u);
    outData.vertexTriangleIndices.resize(indices.size());
    outData.visitedTriangles.resize(triangleCount, 0u);

    for(const u32 vertexRefIndex : indices)
        ++outData.vertexTriangleOffsets[vertexRefIndex + 1u];

    for(usize vertexRefIndex = 0u; vertexRefIndex < entry.vertexRefs.size(); ++vertexRefIndex)
        outData.vertexTriangleOffsets[vertexRefIndex + 1u] += outData.vertexTriangleOffsets[vertexRefIndex];

    Core::Assets::AssetVector<u32> vertexTriangleCursor(entry.positions.get_allocator().arena());
    vertexTriangleCursor.insert(
        vertexTriangleCursor.end(),
        outData.vertexTriangleOffsets.begin(),
        outData.vertexTriangleOffsets.end()
    );

    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        MeshletTriangleData& triangle = outData.triangles[triangleIndex];
        const usize indexOffset = triangleIndex * 3u;
        triangle.vertexRefs[0] = indices[indexOffset + 0u];
        triangle.vertexRefs[1] = indices[indexOffset + 1u];
        triangle.vertexRefs[2] = indices[indexOffset + 2u];

        const SIMDVector p0 = LoadMeshletSourceVertexPositionVector(entry, triangle.vertexRefs[0]);
        const SIMDVector p1 = LoadMeshletSourceVertexPositionVector(entry, triangle.vertexRefs[1]);
        const SIMDVector p2 = LoadMeshletSourceVertexPositionVector(entry, triangle.vertexRefs[2]);
        const SIMDVector centroid = VectorScale(VectorAdd(VectorAdd(p0, p1), p2), 1.0f / 3.0f);
        const SIMDVector areaNormal = BuildMeshletFaceNormal(p0, p1, p2);
        StoreFloat(VectorSetW(centroid, 0.0f), &triangle.centroid);
        StoreFloat(VectorSetW(areaNormal, 0.0f), &triangle.areaNormal);

        const u32 triangleIndexU32 = static_cast<u32>(triangleIndex);
        for(const u32 vertexRefIndex : triangle.vertexRefs){
            const u32 adjacencyOffset = vertexTriangleCursor[vertexRefIndex]++;
            outData.vertexTriangleIndices[adjacencyOffset] = triangleIndexU32;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

