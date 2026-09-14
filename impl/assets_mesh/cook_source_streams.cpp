// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_source_streams.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshCookSourceStreams::ParseSourceVertexRefs(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const NotNull<const tchar*> metaKind,
    const bool includeSkin,
    ScratchVector<MeshVertexRef>& outVertexRefs,
    Core::Alloc::ScratchArena& scratchArena
){
    outVertexRefs.clear();

    const Core::Metascript::Value* field = MeshCookMetadata::FindRequiredMetadataListField(
        nwbFilePath,
        asset,
        metaKind,
        "vertex_refs"
    );
    if(!field)
        return false;

    const auto& list = field->asList();
    const usize expectedComponentCount = includeSkin ? 6u : 5u;
    outVertexRefs.reserve(list.size());
    for(usize vertexRefIndex = 0u; vertexRefIndex < list.size(); ++vertexRefIndex){
        const Core::Metascript::Value& value = list[vertexRefIndex];
        if(!value.isList() || value.asList().size() != expectedComponentCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'vertex_refs[{}]' must contain {} integer stream indices")
                , metaKind.get()
                , PathToString<tchar>(nwbFilePath)
                , vertexRefIndex
                , expectedComponentCount
            );
            return false;
        }

        MeshVertexRef ref;
        const auto& components = value.asList();
        const ScratchString label = MeshCookMetadata::MakeIndexedLabel(scratchArena, "vertex_refs", vertexRefIndex);
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
            if(!MeshCookMetadata::ParseMetadataU32Value(
                nwbFilePath,
                components[componentIndex],
                metaKind,
                componentLabel,
                *componentValues[componentIndex]
            ))
                return false;
        }
        outVertexRefs.push_back(ref);
    }

    if(outVertexRefs.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{} meta '{}': 'vertex_refs' must not be empty"),
            metaKind.get(),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
}


bool MeshCookSourceStreams::ValidateSourceStreamIndex(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const AStringView streamName,
    const u32 index,
    const usize streamCount
){
    if(index < streamCount)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': vertex_ref {} index is out of range")
        , metaKind.get()
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(streamName)
    );
    return false;
}


bool MeshCookSourceStreams::ValidateSourceIndexStream(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const usize vertexRefCount
){
    if(indices.empty() || (indices.size() % s_MeshletTriangleIndexCount) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' must contain whole triangles")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    for(const u32 index : indices){
        if(index < vertexRefCount)
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' references an out-of-range vertex_ref")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
}


bool MeshCookSourceStreams::ValidateSourceVertexRefs(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
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
                , metaKind.get()
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
    }
    return true;
}


bool MeshCookSourceStreams::ParseCommonSourceMeshStreams(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    const NotNull<const tchar*> metaKind,
    const bool includeSkin,
    SourceMeshStreams& streams,
    const usize skinCount,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!MeshCookMetadata::ParseMetadataFloatListField<Float3U, 3u>(
        discoveredFile.filePath,
        asset,
        metaKind,
        "positions",
        streams.positions,
        scratchArena
    ))
        return false;
    if(!MeshCookMetadata::ParseMetadataFloatListField<Float3U, 3u>(
        discoveredFile.filePath,
        asset,
        metaKind,
        "normals",
        streams.normals,
        scratchArena
    ))
        return false;
    if(!MeshCookMetadata::ParseMetadataFloatListField<Float4U, 4u>(
        discoveredFile.filePath,
        asset,
        metaKind,
        "tangents",
        streams.tangents,
        scratchArena
    ))
        return false;
    if(!MeshCookMetadata::ParseMetadataFloatListField<Float2U, 2u>(
        discoveredFile.filePath,
        asset,
        metaKind,
        "uv0",
        streams.uv0,
        scratchArena
    ))
        return false;

    if(!MeshCookMetadata::ParseMetadataFloatListField<Float4U, 4u>(
        discoveredFile.filePath,
        asset,
        metaKind,
        "colors",
        streams.colors,
        scratchArena
    ))
        return false;

    if(!ParseSourceVertexRefs(discoveredFile.filePath, asset, metaKind, includeSkin, streams.vertexRefs, scratchArena))
        return false;
    if(!MeshCookMetadata::ParseMetadataIndexField(discoveredFile.filePath, asset, metaKind, streams.indices, scratchArena))
        return false;
    if(!ValidateSourceIndexStream(discoveredFile.filePath, metaKind, streams.indices, streams.vertexRefs.size()))
        return false;
    if(!ValidateSourceVertexRefs(discoveredFile.filePath, metaKind, includeSkin, streams, skinCount))
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

