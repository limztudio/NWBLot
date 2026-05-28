// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PositionRefVectorT>
[[nodiscard]] static bool FindMeshletPositionRef(
    const PositionRefVectorT& refs,
    const MeshletPositionStreamRef& ref,
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
    const MeshletAttributeStreamRef& ref,
    const u32 skin,
    u16& outLocalAttribute
){
    outLocalAttribute = 0u;
    for(usize localIndex = 0u; localIndex < refs.size(); ++localIndex){
        const MeshletAttributeStreamRef& existing = refs[localIndex];
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
        const MeshletPositionStreamRef positionRef{ source.position, source.skin };
        const MeshletAttributeStreamRef attributeRef{ source.normal, source.tangent, source.uv0, source.color };

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

