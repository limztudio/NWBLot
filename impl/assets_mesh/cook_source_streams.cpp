// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_source_streams.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_source_streams{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_VertexRefComponentCountWithSkin = 6u;
inline constexpr usize s_VertexRefComponentCountWithoutSkin = 5u;
inline constexpr AStringView s_VertexRefPositionName = "position";
inline constexpr AStringView s_VertexRefNormalName = "normal";
inline constexpr AStringView s_VertexRefTangentName = "tangent";
inline constexpr AStringView s_VertexRefUvName = "uv0";
inline constexpr AStringView s_VertexRefColorName = "color";
inline constexpr AStringView s_VertexRefSkinName = "skin";
inline constexpr TStringView s_PositionStreamLabel = NWB_TEXT("position");
inline constexpr TStringView s_NormalStreamLabel = NWB_TEXT("normal");
inline constexpr TStringView s_TangentStreamLabel = NWB_TEXT("tangent");
inline constexpr TStringView s_ColorStreamLabel = NWB_TEXT("color");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshCookSourceStreams::ParseSourceVertexRefs(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    const bool includeSkin,
    ScratchVector<MeshVertexRef>& outVertexRefs,
    Core::Alloc::ScratchArena& scratchArena
){
    outVertexRefs.clear();

    const Core::Metascript::Value* field = MeshCookMetadata::FindRequiredMetadataListField(
        nwbFilePath,
        asset,
        metaKind,
        MeshCookMetadata::s_VertexRefsFieldNameView
    );
    if(!field)
        return false;

    const auto& list = field->asList();
    const usize expectedComponentCount = includeSkin ? __hidden_mesh_source_streams::s_VertexRefComponentCountWithSkin : __hidden_mesh_source_streams::s_VertexRefComponentCountWithoutSkin;
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
        const ScratchString label = MeshCookMetadata::MakeIndexedLabel(scratchArena, MeshCookMetadata::s_VertexRefsFieldNameView, vertexRefIndex);
        const AStringView componentNames[] = {
            __hidden_mesh_source_streams::s_VertexRefPositionName,
            __hidden_mesh_source_streams::s_VertexRefNormalName,
            __hidden_mesh_source_streams::s_VertexRefTangentName,
            __hidden_mesh_source_streams::s_VertexRefUvName,
            __hidden_mesh_source_streams::s_VertexRefColorName,
            __hidden_mesh_source_streams::s_VertexRefSkinName,
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
            metaKind,
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
}


bool MeshCookSourceStreams::ValidateSourceStreamIndex(
    const Path& nwbFilePath,
    const TStringView metaKind,
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


bool MeshCookSourceStreams::ValidateSourceIndexStream(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const usize vertexRefCount
){
    if(indices.empty() || (indices.size() % s_MeshletTriangleIndexCount) != 0u){
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


bool MeshCookSourceStreams::ValidateSourceVertexRefs(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const bool includeSkin,
    const SourceMeshStreams& streams,
    const usize skinCount
){
    for(const MeshVertexRef& ref : streams.vertexRefs){
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, __hidden_mesh_source_streams::s_VertexRefPositionName, ref.position, streams.positions.size()))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, __hidden_mesh_source_streams::s_VertexRefNormalName, ref.normal, streams.normals.size()))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, __hidden_mesh_source_streams::s_VertexRefTangentName, ref.tangent, streams.tangents.size()))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, __hidden_mesh_source_streams::s_VertexRefUvName, ref.uv0, streams.uv0.size()))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, __hidden_mesh_source_streams::s_VertexRefColorName, ref.color, streams.colors.size()))
            return false;
        if(includeSkin){
            if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, __hidden_mesh_source_streams::s_VertexRefSkinName, ref.skin, skinCount))
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


bool MeshCookSourceStreams::ParseCommonSourceMeshStreams(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    const bool includeSkin,
    SourceMeshStreams& streams,
    const usize skinCount,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!MeshCookMetadata::ParseMetadataFloatListField<Float3U, 3u>(
        discoveredFile.filePath,
        asset,
        metaKind,
        MeshCookMetadata::s_PositionsFieldNameView,
        streams.positions,
        scratchArena
    ))
        return false;
    if(!MeshCookMetadata::ParseMetadataFloatListField<Float3U, 3u>(
        discoveredFile.filePath,
        asset,
        metaKind,
        MeshCookMetadata::s_NormalsFieldNameView,
        streams.normals,
        scratchArena
    ))
        return false;
    if(!MeshCookMetadata::ParseMetadataFloatListField<Float4U, 4u>(
        discoveredFile.filePath,
        asset,
        metaKind,
        MeshCookMetadata::s_TangentsFieldNameView,
        streams.tangents,
        scratchArena
    ))
        return false;
    if(!MeshCookMetadata::ParseMetadataFloatListField<Float2U, 2u>(
        discoveredFile.filePath,
        asset,
        metaKind,
        MeshCookMetadata::s_Uv0FieldNameView,
        streams.uv0,
        scratchArena
    ))
        return false;

    if(!MeshCookMetadata::ParseMetadataFloatListField<Float4U, 4u>(
        discoveredFile.filePath,
        asset,
        metaKind,
        MeshCookMetadata::s_ColorsFieldNameView,
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

