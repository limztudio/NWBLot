struct SourceMeshStreams{
    ScratchVector<Float3U> positions;
    ScratchVector<Float3U> normals;
    ScratchVector<Float4U> tangents;
    ScratchVector<Float2U> uv0;
    ScratchVector<Float4U> colors;
    ScratchVector<MeshVertexRef> vertexRefs;
    Core::Assets::AssetVector<u32> indices;

    SourceMeshStreams(Core::Assets::AssetArena& assetArena, Core::Alloc::ScratchArena& scratchArena)
        : positions(scratchArena)
        , normals(scratchArena)
        , tangents(scratchArena)
        , uv0(scratchArena)
        , colors(scratchArena)
        , vertexRefs(scratchArena)
        , indices(assetArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseSourceVertexRefs(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const tchar* metaKind,
    const bool includeSkin,
    ScratchVector<MeshVertexRef>& outVertexRefs,
    Core::Alloc::ScratchArena& scratchArena
){
    outVertexRefs.clear();

    const Core::Metascript::Value* field = FindRequiredMetadataListField(nwbFilePath, asset, metaKind, "vertex_refs");
    if(!field)
        return false;

    const auto& list = field->asList();
    const usize expectedComponentCount = includeSkin ? 6u : 5u;
    outVertexRefs.reserve(list.size());
    for(usize vertexRefIndex = 0u; vertexRefIndex < list.size(); ++vertexRefIndex){
        const Core::Metascript::Value& value = list[vertexRefIndex];
        if(!value.isList() || value.asList().size() != expectedComponentCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'vertex_refs[{}]' must contain {} integer stream indices")
                , metaKind
                , PathToString<tchar>(nwbFilePath)
                , vertexRefIndex
                , expectedComponentCount
            );
            return false;
        }

        MeshVertexRef ref;
        const auto& components = value.asList();
        const ScratchString label = MakeIndexedLabel(scratchArena, "vertex_refs", vertexRefIndex);
        const AStringView componentNames[] = {
            "position",
            "normal",
            "tangent",
            "uv0",
            "color",
            "skin",
        };
        u32* const componentValues[] = {
            &ref.position,
            &ref.normal,
            &ref.tangent,
            &ref.uv0,
            &ref.color,
            &ref.skin,
        };
        for(usize componentIndex = 0u; componentIndex < expectedComponentCount; ++componentIndex){
            ScratchString componentLabel{scratchArena};
            componentLabel.reserve(label.size() + componentNames[componentIndex].size() + 2u);
            componentLabel.append(label.data(), label.size());
            componentLabel += '.';
            componentLabel.append(componentNames[componentIndex].data(), componentNames[componentIndex].size());
            if(!ParseMetadataU32Value(nwbFilePath, components[componentIndex], metaKind, componentLabel, *componentValues[componentIndex]))
                return false;
        }
        outVertexRefs.push_back(ref);
    }

    if(outVertexRefs.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'vertex_refs' must not be empty"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }
    return true;
}

static bool ValidateSourceStreamIndex(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const AStringView streamName,
    const u32 index,
    const usize streamCount
){
    if(index < streamCount)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': vertex_ref {} index is out of range")
        , metaKind
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(streamName)
    );
    return false;
}

static bool ValidateSourceIndexStream(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const usize vertexRefCount
){
    if(indices.empty() || (indices.size() % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' must contain whole triangles")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    for(const u32 index : indices){
        if(index < vertexRefCount)
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' references an out-of-range vertex_ref")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
}

static bool ValidateSourceVertexRefs(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const bool includeSkin,
    const SourceMeshStreams& streams,
    const usize skinCount
){
    for(const MeshVertexRef& ref : streams.vertexRefs){
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, "position", ref.position, streams.positions.size()))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, "normal", ref.normal, streams.normals.size()))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, "tangent", ref.tangent, streams.tangents.size()))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, "uv0", ref.uv0, streams.uv0.size()))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, "color", ref.color, streams.colors.size()))
            return false;
        if(includeSkin){
            if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, "skin", ref.skin, skinCount))
                return false;
        }
        else if(ref.skin != s_MeshMissingStreamIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': static vertex_ref cannot contain a skin index")
                , metaKind
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
    }
    return true;
}

template<typename CookEntryT>
static void CopySourceStreams(SourceMeshStreams& streams, CookEntryT& outEntry){
    outEntry.positions.clear();
    outEntry.normals.clear();
    outEntry.tangents.clear();
    outEntry.uv0.clear();
    outEntry.colors.clear();
    outEntry.vertexRefs.clear();

    outEntry.positions.reserve(streams.positions.size());
    outEntry.normals.reserve(streams.normals.size());
    outEntry.tangents.reserve(streams.tangents.size());
    outEntry.uv0.reserve(streams.uv0.size());
    outEntry.colors.reserve(streams.colors.size());
    outEntry.vertexRefs.reserve(streams.vertexRefs.size());

    outEntry.positions.insert(outEntry.positions.end(), streams.positions.begin(), streams.positions.end());
    for(const Float3U& normal : streams.normals)
        outEntry.normals.push_back(MakeHalf4U(normal.x, normal.y, normal.z, 0.0f));
    for(const Float4U& tangent : streams.tangents)
        outEntry.tangents.push_back(MakeHalf4U(tangent.x, tangent.y, tangent.z, tangent.w));
    outEntry.uv0.insert(outEntry.uv0.end(), streams.uv0.begin(), streams.uv0.end());
    for(const Float4U& color : streams.colors)
        outEntry.colors.push_back(MakeHalf4U(color.x, color.y, color.z, color.w));
    outEntry.vertexRefs.insert(outEntry.vertexRefs.end(), streams.vertexRefs.begin(), streams.vertexRefs.end());
}

#include "cook_meshlet.inl"

static bool ParseCommonSourceMeshStreams(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    const tchar* metaKind,
    const bool includeSkin,
    SourceMeshStreams& streams,
    const usize skinCount,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!ParseMetadataFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, metaKind, "positions", streams.positions, scratchArena))
        return false;
    if(!ParseMetadataFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, metaKind, "normals", streams.normals, scratchArena))
        return false;
    if(!ParseMetadataFloatListField<Float4U, 4u>(discoveredFile.filePath, asset, metaKind, "tangents", streams.tangents, scratchArena))
        return false;
    if(!ParseMetadataFloatListField<Float2U, 2u>(discoveredFile.filePath, asset, metaKind, "uv0", streams.uv0, scratchArena))
        return false;

    if(!ParseMetadataFloatListField<Float4U, 4u>(discoveredFile.filePath, asset, metaKind, "colors", streams.colors, scratchArena))
        return false;

    if(!ParseSourceVertexRefs(discoveredFile.filePath, asset, metaKind, includeSkin, streams.vertexRefs, scratchArena))
        return false;
    if(!ParseMetadataIndexField(discoveredFile.filePath, asset, metaKind, streams.indices, scratchArena))
        return false;
    if(!ValidateSourceIndexStream(discoveredFile.filePath, metaKind, streams.indices, streams.vertexRefs.size()))
        return false;
    if(!ValidateSourceVertexRefs(discoveredFile.filePath, metaKind, includeSkin, streams, skinCount))
        return false;
    return true;
}

static bool ParseSourceMeshMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    MeshCookEntry& outEntry,
    Core::Alloc::ThreadPool& threadPool,
    Core::Alloc::ScratchArena& scratchArena
){
    SourceMeshStreams streams(outEntry.positions.get_allocator().arena(), scratchArena);
    if(!ParseCommonSourceMeshStreams(discoveredFile, asset, s_MeshMetaKind, false, streams, 0u, scratchArena))
        return false;

    CopySourceStreams(streams, outEntry);
    return BuildMeshlets(discoveredFile.filePath, s_MeshMetaKind, streams.indices, outEntry, threadPool);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

