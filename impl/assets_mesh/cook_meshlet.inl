// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CookEntryT>
[[nodiscard]] static SIMDVector LoadMeshletPositionVector(
    const CookEntryT& entry,
    const MeshletDeformedPositionRef& ref
){
    return VectorSetW(LoadFloat(entry.positions[ref.position]), 0.0f);
}

template<typename CookEntryT>
[[nodiscard]] static SIMDVector LoadMeshletLocalPositionVector(
    const CookEntryT& entry,
    const MeshletDesc& meshlet,
    const u32 localVertexIndex
){
    const MeshletLocalVertexRef& localVertexRef = entry.meshletLocalVertexRefs[meshlet.localVertexOffset + localVertexIndex];
    const usize positionRefIndex = meshlet.positionOffset + localVertexRef.localDeformedPosition;
    const MeshletDeformedPositionRef& positionRef = entry.meshletPositionRefs[positionRefIndex];
    return LoadMeshletPositionVector(entry, positionRef);
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
    for(u32 primitiveIndex = 0u; primitiveIndex < MeshletPrimitiveCount(meshlet); ++primitiveIndex){
        const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * 3u;
        const u8 localVertex0 = entry.meshletPrimitiveIndices[primitiveOffset + 0u];
        const u8 localVertex1 = entry.meshletPrimitiveIndices[primitiveOffset + 1u];
        const u8 localVertex2 = entry.meshletPrimitiveIndices[primitiveOffset + 2u];
        const SIMDVector p0 = LoadMeshletLocalPositionVector(entry, meshlet, localVertex0);
        const SIMDVector p1 = LoadMeshletLocalPositionVector(entry, meshlet, localVertex1);
        const SIMDVector p2 = LoadMeshletLocalPositionVector(entry, meshlet, localVertex2);
        callback(BuildMeshletFaceNormal(p0, p1, p2));
    }
}

template<typename CookEntryT>
static MeshletBounds BuildMeshletBounds(const CookEntryT& entry, const MeshletDesc& meshlet){
    SIMDVector minBounds = VectorReplicate(Limit<f32>::s_Max);
    SIMDVector maxBounds = VectorReplicate(-Limit<f32>::s_Max);
    for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
        const MeshletDeformedPositionRef& ref = entry.meshletPositionRefs[meshlet.positionOffset + localPositionIndex];
        const SIMDVector position = LoadMeshletPositionVector(entry, ref);
        minBounds = VectorMin(minBounds, position);
        maxBounds = VectorMax(maxBounds, position);
    }

    const SIMDVector center = VectorSetW(VectorScale(VectorAdd(minBounds, maxBounds), 0.5f), 0.0f);
    SIMDVector radiusSquared = VectorZero();
    for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
        const MeshletDeformedPositionRef& ref = entry.meshletPositionRefs[meshlet.positionOffset + localPositionIndex];
        const SIMDVector delta = VectorSubtract(LoadMeshletPositionVector(entry, ref), center);
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
    bounds.conePacked = PackMeshletCone(coneAxis, coneCutoff);
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

template<typename PositionRefVectorT>
[[nodiscard]] static bool FindMeshletPositionRef(
    const PositionRefVectorT& refs,
    const MeshletDeformedPositionRef& ref,
    u16& outLocalPosition
){
    outLocalPosition = 0u;
    for(usize localIndex = 0u; localIndex < refs.size(); ++localIndex){
        if(refs[localIndex].position != ref.position || refs[localIndex].skin != ref.skin)
            continue;

        outLocalPosition = static_cast<u16>(localIndex);
        return true;
    }

    return false;
}

template<typename AttributeRefVectorT, typename AttributeSkinVectorT>
[[nodiscard]] static bool FindMeshletAttributeRef(
    const AttributeRefVectorT& refs,
    const AttributeSkinVectorT& skins,
    const MeshletShadingAttributeRef& ref,
    const u32 skin,
    u16& outLocalAttribute
){
    outLocalAttribute = 0u;
    for(usize localIndex = 0u; localIndex < refs.size(); ++localIndex){
        const MeshletShadingAttributeRef& existing = refs[localIndex];
        if(
            existing.normal != ref.normal
            || existing.tangent != ref.tangent
            || existing.uv0 != ref.uv0
            || existing.color != ref.color
            || skins[localIndex] != skin
        )
            continue;

        outLocalAttribute = static_cast<u16>(localIndex);
        return true;
    }

    return false;
}

template<
    typename CookEntryT,
    typename LocalVertexVectorT,
    typename PositionRefVectorT,
    typename AttributeRefVectorT,
    typename AttributeSkinVectorT,
    typename LocalVertexRefVectorT
>
[[nodiscard]] static bool BuildZippedMeshletRefs(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const CookEntryT& entry,
    const LocalVertexVectorT& sourceVertexRefs,
    PositionRefVectorT& outPositionRefs,
    AttributeRefVectorT& outAttributeRefs,
    AttributeSkinVectorT& outAttributeSkins,
    LocalVertexRefVectorT& outLocalVertexRefs
){
    outPositionRefs.clear();
    outAttributeRefs.clear();
    outAttributeSkins.clear();
    outLocalVertexRefs.clear();
    outPositionRefs.reserve(sourceVertexRefs.size());
    outAttributeRefs.reserve(sourceVertexRefs.size());
    outAttributeSkins.reserve(sourceVertexRefs.size());
    outLocalVertexRefs.reserve(sourceVertexRefs.size());

    for(const u32 vertexRefIndex : sourceVertexRefs){
        const MeshVertexRef& source = entry.vertexRefs[vertexRefIndex];
        const MeshletDeformedPositionRef positionRef{ source.position, source.skin };
        const MeshletShadingAttributeRef attributeRef{ source.normal, source.tangent, source.uv0, source.color };

        u16 localPosition = 0u;
        if(!FindMeshletPositionRef(outPositionRefs, positionRef, localPosition)){
            if(outPositionRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet deformed positions exceed local index limits")
                    , metaKind
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
            localPosition = static_cast<u16>(outPositionRefs.size());
            outPositionRefs.push_back(positionRef);
        }

        u16 localAttribute = 0u;
        if(!FindMeshletAttributeRef(outAttributeRefs, outAttributeSkins, attributeRef, source.skin, localAttribute)){
            if(outAttributeRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet attributes exceed local index limits")
                    , metaKind
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
            localAttribute = static_cast<u16>(outAttributeRefs.size());
            outAttributeRefs.push_back(attributeRef);
            outAttributeSkins.push_back(source.skin);
        }

        outLocalVertexRefs.push_back(MeshletLocalVertexRef{ localPosition, localAttribute });
    }

    return true;
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
    entry.meshletPositionRefs.clear();
    entry.meshletAttributeRefs.clear();
    entry.meshletLocalVertexRefs.clear();
    entry.meshletPrimitiveIndices.clear();
    entry.meshlets.reserve((indices.size() / 3u + s_MeshMaxMeshletTriangles - 1u) / s_MeshMaxMeshletTriangles);
    entry.meshletBounds.reserve(entry.meshlets.capacity());
    entry.meshletPositionRefs.reserve(indices.size());
    entry.meshletAttributeRefs.reserve(indices.size());
    entry.meshletLocalVertexRefs.reserve(indices.size());
    entry.meshletPrimitiveIndices.reserve(indices.size());

    MeshletDesc current;
    Core::Assets::AssetVector<u32> localSourceVertexRefs(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<MeshletDeformedPositionRef> localPositionRefs(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<MeshletShadingAttributeRef> localAttributeRefs(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<u32> localAttributeSkins(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<MeshletLocalVertexRef> localVertexRefs(entry.positions.get_allocator().arena());
    localSourceVertexRefs.reserve(s_MeshMaxMeshletVertices);
    localPositionRefs.reserve(s_MeshMaxMeshletVertices);
    localAttributeRefs.reserve(s_MeshMaxMeshletVertices);
    localAttributeSkins.reserve(s_MeshMaxMeshletVertices);
    localVertexRefs.reserve(s_MeshMaxMeshletVertices);

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
        current.positionOffset = static_cast<u32>(entry.meshletPositionRefs.size());
        current.attributeOffset = static_cast<u32>(entry.meshletAttributeRefs.size());
        current.counts = PackMeshletCounts(
            static_cast<u32>(localVertexRefs.size()),
            primitiveCount,
            static_cast<u32>(localPositionRefs.size()),
            static_cast<u32>(localAttributeRefs.size())
        );

        entry.meshletPositionRefs.insert(entry.meshletPositionRefs.end(), localPositionRefs.begin(), localPositionRefs.end());
        entry.meshletAttributeRefs.insert(
            entry.meshletAttributeRefs.end(),
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

        current = MeshletDesc{};
        current.primitiveOffset = static_cast<u32>(entry.meshletPrimitiveIndices.size());
        localSourceVertexRefs.clear();
        return true;
    };

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
            if(!FindMeshletLocalVertex(localSourceVertexRefs, vertexRefIndex, localVertex))
                ++missingCount;
        }

        if(
            MeshletPrimitiveCount(current) != 0u
            && (
                localSourceVertexRefs.size() + missingCount > s_MeshMaxMeshletVertices
                || MeshletPrimitiveCount(current) + 1u > s_MeshMaxMeshletTriangles
            )
        ){
            if(!flushMeshlet())
                return false;
        }

        for(const u32 vertexRefIndex : triangleRefs){
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
            entry.meshletPrimitiveIndices.push_back(localVertex);
        }
        current.counts = PackMeshletCounts(0u, MeshletPrimitiveCount(current) + 1u, 0u, 0u);
    }
    if(!flushMeshlet())
        return false;

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

