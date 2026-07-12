
template<typename CookEntryT>
[[nodiscard]] static bool PrecomputeMeshletTriangleData(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const CookEntryT& entry,
    MeshletTrianglePrecompute& outData
){
    outData.triangles.clear();
    outData.positionTriangleOffsets.clear();
    outData.positionTriangleIndices.clear();
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
    outData.positionTriangleOffsets.resize(entry.positions.size() + 1u, 0u);
    outData.positionTriangleIndices.resize(indices.size());
    outData.visitedTriangles.resize(triangleCount, 0u);

    for(const u32 vertexRefIndex : indices){
        const MeshVertexRef& vertexRef = entry.vertexRefs[vertexRefIndex];
        ++outData.positionTriangleOffsets[vertexRef.position + 1u];
    }

    for(usize positionIndex = 0u; positionIndex < entry.positions.size(); ++positionIndex)
        outData.positionTriangleOffsets[positionIndex + 1u] += outData.positionTriangleOffsets[positionIndex];

    Core::Assets::AssetVector<u32> positionTriangleCursor(entry.positions.get_allocator().arena());
    positionTriangleCursor.insert(
        positionTriangleCursor.end(),
        outData.positionTriangleOffsets.begin(),
        outData.positionTriangleOffsets.end()
    );

    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        MeshletTriangleData& triangle = outData.triangles[triangleIndex];
        const usize indexOffset = triangleIndex * 3u;
        triangle.vertexRefs[0] = indices[indexOffset + 0u];
        triangle.vertexRefs[1] = indices[indexOffset + 1u];
        triangle.vertexRefs[2] = indices[indexOffset + 2u];
        triangle.positions[0] = entry.vertexRefs[triangle.vertexRefs[0]].position;
        triangle.positions[1] = entry.vertexRefs[triangle.vertexRefs[1]].position;
        triangle.positions[2] = entry.vertexRefs[triangle.vertexRefs[2]].position;

        const SIMDVector p0 = VectorSetW(
            LoadFloat(MeshletSourceVertexPositionStreamValue(entry, triangle.vertexRefs[0])),
            0.0f
        );
        const SIMDVector p1 = VectorSetW(
            LoadFloat(MeshletSourceVertexPositionStreamValue(entry, triangle.vertexRefs[1])),
            0.0f
        );
        const SIMDVector p2 = VectorSetW(
            LoadFloat(MeshletSourceVertexPositionStreamValue(entry, triangle.vertexRefs[2])),
            0.0f
        );
        StoreFloat(p0, &triangle.positionVectors[0]);
        StoreFloat(p1, &triangle.positionVectors[1]);
        StoreFloat(p2, &triangle.positionVectors[2]);
        const SIMDVector centroid = VectorScale(VectorAdd(VectorAdd(p0, p1), p2), 1.0f / 3.0f);
        const SIMDVector areaNormal = TriangleTests::AreaNormal(p0, p1, p2);
        StoreFloat(VectorSetW(centroid, 0.0f), &triangle.centroid);
        StoreFloat(VectorSetW(areaNormal, 0.0f), &triangle.areaNormal);

        const u32 triangleIndexU32 = static_cast<u32>(triangleIndex);
        for(const u32 positionIndex : triangle.positions){
            const u32 adjacencyOffset = positionTriangleCursor[positionIndex]++;
            outData.positionTriangleIndices[adjacencyOffset] = triangleIndexU32;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

