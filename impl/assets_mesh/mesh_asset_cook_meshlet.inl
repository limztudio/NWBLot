// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Float3U LoadPositionRef(
    const Core::Assets::AssetVector<Float3U>& positions,
    const Core::Assets::AssetVector<MeshVertexRef>& vertexRefs,
    const u32 vertexRefIndex
){
    return positions[vertexRefs[vertexRefIndex].position];
}

[[nodiscard]] static Float3U Cross(const Float3U& a, const Float3U& b){
    return Float3U(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

[[nodiscard]] static f32 Dot(const Float3U& a, const Float3U& b){
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] static Float3U Subtract(const Float3U& a, const Float3U& b){
    return Float3U(a.x - b.x, a.y - b.y, a.z - b.z);
}

[[nodiscard]] static Float3U NormalizeOrZero(const Float3U& value){
    const f32 lengthSquared = Dot(value, value);
    if(!IsFinite(lengthSquared) || lengthSquared <= SkinnedMeshValidation::s_Epsilon)
        return Float3U(0.0f, 0.0f, 0.0f);

    const f32 inverseLength = 1.0f / Sqrt(lengthSquared);
    return Float3U(value.x * inverseLength, value.y * inverseLength, value.z * inverseLength);
}

template<typename CookEntryT>
[[nodiscard]] static Float3U BuildMeshletFaceNormal(
    const CookEntryT& entry,
    const MeshletDesc& meshlet,
    const usize primitiveOffset
){
    const u32 i0 = entry.meshletVertexRefs[meshlet.vertexOffset + entry.meshletPrimitiveIndices[primitiveOffset + 0u]];
    const u32 i1 = entry.meshletVertexRefs[meshlet.vertexOffset + entry.meshletPrimitiveIndices[primitiveOffset + 1u]];
    const u32 i2 = entry.meshletVertexRefs[meshlet.vertexOffset + entry.meshletPrimitiveIndices[primitiveOffset + 2u]];
    const Float3U p0 = LoadPositionRef(entry.positions, entry.vertexRefs, i0);
    return Cross(
        Subtract(LoadPositionRef(entry.positions, entry.vertexRefs, i1), p0),
        Subtract(LoadPositionRef(entry.positions, entry.vertexRefs, i2), p0)
    );
}

template<typename CookEntryT>
static MeshletBounds BuildMeshletBounds(const CookEntryT& entry, const MeshletDesc& meshlet){
    Float3U minBounds(Limit<f32>::s_Max, Limit<f32>::s_Max, Limit<f32>::s_Max);
    Float3U maxBounds(-Limit<f32>::s_Max, -Limit<f32>::s_Max, -Limit<f32>::s_Max);
    for(u32 localVertexIndex = 0u; localVertexIndex < meshlet.vertexCount; ++localVertexIndex){
        const u32 vertexRefIndex = entry.meshletVertexRefs[meshlet.vertexOffset + localVertexIndex];
        const Float3U position = LoadPositionRef(entry.positions, entry.vertexRefs, vertexRefIndex);
        minBounds.x = Min(minBounds.x, position.x);
        minBounds.y = Min(minBounds.y, position.y);
        minBounds.z = Min(minBounds.z, position.z);
        maxBounds.x = Max(maxBounds.x, position.x);
        maxBounds.y = Max(maxBounds.y, position.y);
        maxBounds.z = Max(maxBounds.z, position.z);
    }

    const Float3U center(
        (minBounds.x + maxBounds.x) * 0.5f,
        (minBounds.y + maxBounds.y) * 0.5f,
        (minBounds.z + maxBounds.z) * 0.5f
    );
    f32 radiusSquared = 0.0f;
    for(u32 localVertexIndex = 0u; localVertexIndex < meshlet.vertexCount; ++localVertexIndex){
        const u32 vertexRefIndex = entry.meshletVertexRefs[meshlet.vertexOffset + localVertexIndex];
        const Float3U delta = Subtract(LoadPositionRef(entry.positions, entry.vertexRefs, vertexRefIndex), center);
        radiusSquared = Max(radiusSquared, Dot(delta, delta));
    }

    Float3U areaWeightedNormal(0.0f, 0.0f, 0.0f);
    for(u32 primitiveIndex = 0u; primitiveIndex < meshlet.primitiveCount; ++primitiveIndex){
        const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * 3u;
        const Float3U faceNormal = BuildMeshletFaceNormal(entry, meshlet, primitiveOffset);
        areaWeightedNormal.x += faceNormal.x;
        areaWeightedNormal.y += faceNormal.y;
        areaWeightedNormal.z += faceNormal.z;
    }

    const Float3U coneAxis = NormalizeOrZero(areaWeightedNormal);
    f32 coneCutoff = -1.0f;
    if(Dot(coneAxis, coneAxis) > 0.0f){
        coneCutoff = 1.0f;
        for(u32 primitiveIndex = 0u; primitiveIndex < meshlet.primitiveCount; ++primitiveIndex){
            const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * 3u;
            const Float3U faceNormal = NormalizeOrZero(BuildMeshletFaceNormal(entry, meshlet, primitiveOffset));
            if(Dot(faceNormal, faceNormal) > 0.0f)
                coneCutoff = Min(coneCutoff, Dot(coneAxis, faceNormal));
        }
        if(coneCutoff <= 0.0f)
            coneCutoff = -1.0f;
    }

    MeshletBounds bounds;
    bounds.sphere = Float4U(center.x, center.y, center.z, Sqrt(radiusSquared));
    bounds.cone = Float4U(coneAxis.x, coneAxis.y, coneAxis.z, coneCutoff);
    return bounds;
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
            bool found = false;
            for(const u32 localRef : localVertexRefs){
                if(localRef == vertexRefIndex){
                    found = true;
                    break;
                }
            }
            if(!found)
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
            bool found = false;
            for(usize localIndex = 0u; localIndex < localVertexRefs.size(); ++localIndex){
                if(localVertexRefs[localIndex] != vertexRefIndex)
                    continue;

                localVertex = static_cast<u8>(localIndex);
                found = true;
                break;
            }
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

