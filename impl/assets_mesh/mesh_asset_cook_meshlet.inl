// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CookEntryT>
[[nodiscard]] static SIMDVector LoadMeshletPositionVector(
    const CookEntryT& entry,
    const u32 vertexRefIndex
){
    return VectorSetW(LoadFloat(entry.positions[entry.vertexRefs[vertexRefIndex].position]), 0.0f);
}

[[nodiscard]] static SIMDVector BuildMeshletFaceNormal(
    const SIMDVector p0,
    const SIMDVector p1,
    const SIMDVector p2
){
    return Vector3Cross(VectorSubtract(p1, p0), VectorSubtract(p2, p0));
}

[[nodiscard]] static SIMDVector NormalizeMeshletDirectionOrZero(const SIMDVector value){
    if(!Core::Mesh::FrameValidDirection(value))
        return VectorZero();

    return VectorSetW(Vector3Normalize(value), 0.0f);
}

template<typename CookEntryT, typename CallbackT>
static void ForEachMeshletFaceNormalVector(const CookEntryT& entry, const MeshletDesc& meshlet, CallbackT callback){
    for(u32 primitiveIndex = 0u; primitiveIndex < meshlet.primitiveCount; ++primitiveIndex){
        const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * 3u;
        const u32 vertexRefIndex0 = entry.meshletVertexRefs[meshlet.vertexOffset + entry.meshletPrimitiveIndices[primitiveOffset + 0u]];
        const u32 vertexRefIndex1 = entry.meshletVertexRefs[meshlet.vertexOffset + entry.meshletPrimitiveIndices[primitiveOffset + 1u]];
        const u32 vertexRefIndex2 = entry.meshletVertexRefs[meshlet.vertexOffset + entry.meshletPrimitiveIndices[primitiveOffset + 2u]];
        const SIMDVector p0 = LoadMeshletPositionVector(entry, vertexRefIndex0);
        const SIMDVector p1 = LoadMeshletPositionVector(entry, vertexRefIndex1);
        const SIMDVector p2 = LoadMeshletPositionVector(entry, vertexRefIndex2);
        callback(BuildMeshletFaceNormal(p0, p1, p2));
    }
}

template<typename CookEntryT>
static MeshletBounds BuildMeshletBounds(const CookEntryT& entry, const MeshletDesc& meshlet){
    SIMDVector minBounds = VectorReplicate(Limit<f32>::s_Max);
    SIMDVector maxBounds = VectorReplicate(-Limit<f32>::s_Max);
    for(u32 localVertexIndex = 0u; localVertexIndex < meshlet.vertexCount; ++localVertexIndex){
        const u32 vertexRefIndex = entry.meshletVertexRefs[meshlet.vertexOffset + localVertexIndex];
        const SIMDVector position = LoadMeshletPositionVector(entry, vertexRefIndex);
        minBounds = VectorMin(minBounds, position);
        maxBounds = VectorMax(maxBounds, position);
    }

    const SIMDVector center = VectorSetW(VectorScale(VectorAdd(minBounds, maxBounds), 0.5f), 0.0f);
    SIMDVector radiusSquared = VectorZero();
    for(u32 localVertexIndex = 0u; localVertexIndex < meshlet.vertexCount; ++localVertexIndex){
        const u32 vertexRefIndex = entry.meshletVertexRefs[meshlet.vertexOffset + localVertexIndex];
        const SIMDVector delta = VectorSubtract(LoadMeshletPositionVector(entry, vertexRefIndex), center);
        radiusSquared = VectorMax(radiusSquared, Vector3LengthSq(delta));
    }

    SIMDVector areaWeightedNormal = VectorZero();
    ForEachMeshletFaceNormalVector(entry, meshlet, [&](const SIMDVector faceNormal){
        areaWeightedNormal = VectorAdd(areaWeightedNormal, faceNormal);
    });

    const SIMDVector coneAxis = NormalizeMeshletDirectionOrZero(areaWeightedNormal);
    f32 coneCutoff = -1.0f;
    if(Core::Mesh::FrameValidDirection(coneAxis)){
        coneCutoff = 1.0f;
        ForEachMeshletFaceNormalVector(entry, meshlet, [&](const SIMDVector meshletFaceNormal){
            const SIMDVector faceNormal = NormalizeMeshletDirectionOrZero(meshletFaceNormal);
            if(Core::Mesh::FrameValidDirection(faceNormal))
                coneCutoff = Min(coneCutoff, VectorGetX(Vector3Dot(coneAxis, faceNormal)));
        });
        if(coneCutoff <= 0.0f)
            coneCutoff = -1.0f;
    }

    MeshletBounds bounds;
    StoreFloat(VectorSetW(center, VectorGetX(VectorSqrt(radiusSquared))), &bounds.sphere);
    StoreFloat(VectorSetW(coneAxis, coneCutoff), &bounds.cone);
    return bounds;
}

template<typename VertexRefVectorT>
[[nodiscard]] static bool FindMeshletLocalVertex(
    const VertexRefVectorT& localVertexRefs,
    const u32 vertexRefIndex,
    u8& outLocalVertex
){
    outLocalVertex = 0u;
    for(usize localIndex = 0u; localIndex < localVertexRefs.size(); ++localIndex){
        if(localVertexRefs[localIndex] != vertexRefIndex)
            continue;

        outLocalVertex = static_cast<u8>(localIndex);
        return true;
    }

    return false;
}

template<typename CookEntryT>
static bool BuildMeshlets(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    CookEntryT& entry
){
    entry.meshlets.clear();
    entry.meshletBounds.clear();
    entry.meshletVertexRefs.clear();
    entry.meshletPrimitiveIndices.clear();
    entry.meshlets.reserve((indices.size() / 3u + s_MeshMaxMeshletTriangles - 1u) / s_MeshMaxMeshletTriangles);
    entry.meshletBounds.reserve(entry.meshlets.capacity());
    entry.meshletVertexRefs.reserve(indices.size());
    entry.meshletPrimitiveIndices.reserve(indices.size());

    MeshletDesc current;
    Core::Assets::AssetVector<u32> localVertexRefs(entry.positions.get_allocator().arena());
    localVertexRefs.reserve(s_MeshMaxMeshletVertices);

    auto flushMeshlet = [&](){
        if(current.primitiveCount == 0u)
            return;

        current.vertexCount = static_cast<u32>(localVertexRefs.size());
        entry.meshlets.push_back(current);
        entry.meshletVertexRefs.insert(entry.meshletVertexRefs.end(), localVertexRefs.begin(), localVertexRefs.end());
        entry.meshletBounds.push_back(BuildMeshletBounds(entry, current));
        current = MeshletDesc{
            static_cast<u32>(entry.meshletVertexRefs.size()),
            static_cast<u32>(entry.meshletPrimitiveIndices.size()),
            0u,
            0u,
        };
        localVertexRefs.clear();
    };

    current.vertexOffset = 0u;
    current.primitiveOffset = 0u;
    for(usize indexOffset = 0u; indexOffset < indices.size(); indexOffset += 3u){
        u32 triangleRefs[3] = {
            indices[indexOffset + 0u],
            indices[indexOffset + 1u],
            indices[indexOffset + 2u],
        };

        u32 missingCount = 0u;
        for(const u32 vertexRefIndex : triangleRefs){
            u8 localVertex = 0u;
            if(!FindMeshletLocalVertex(localVertexRefs, vertexRefIndex, localVertex))
                ++missingCount;
        }

        if(
            current.primitiveCount != 0u
            && (
                localVertexRefs.size() + missingCount > s_MeshMaxMeshletVertices
                || current.primitiveCount + 1u > s_MeshMaxMeshletTriangles
            )
        )
            flushMeshlet();

        for(const u32 vertexRefIndex : triangleRefs){
            u8 localVertex = 0u;
            const bool found = FindMeshletLocalVertex(localVertexRefs, vertexRefIndex, localVertex);
            if(!found){
                if(localVertexRefs.size() >= s_MeshMaxMeshletVertices){
                    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': triangle cannot fit within one meshlet")
                        , metaKind
                        , PathToString<tchar>(nwbFilePath)
                    );
                    return false;
                }
                localVertex = static_cast<u8>(localVertexRefs.size());
                localVertexRefs.push_back(vertexRefIndex);
            }
            entry.meshletPrimitiveIndices.push_back(localVertex);
        }
        ++current.primitiveCount;
    }
    flushMeshlet();

    if(entry.meshlets.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet build produced no meshlets")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

