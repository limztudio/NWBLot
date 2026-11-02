// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_source_streams.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshCookSourceStreams::ValidateSourceStreamIndexWithContext(const AStringView context, const AStringView streamName, const u32 index, const usize streamCount){
    if(index < streamCount)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{}: vertex_ref {} index is out of range"), ConvertString(context), ConvertString(streamName));
    return false;
}

b8 MeshCookSourceStreams::ValidateSourceVertexRefs(const Path& nwbFilePath, const TStringView metaKind, const SourceMeshStreams& streams){
    return ValidateSourceVertexRefStreams(
        nwbFilePath,
        metaKind,
        Span<const MeshVertexRef>(streams.vertexRefs.data(), streams.vertexRefs.size()),
        streams.positions.size(),
        streams.normals.size(),
        streams.tangents.size(),
        streams.uv0.size(),
        streams.colors.size()
    );
}

#if defined(NWB_COOK)
void MeshCookSourceStreams::CopySourceStreams(const SourceMeshStreams& streams, MeshCookEntry& outEntry){
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

Expected<SourceMeshStreams> MeshCookSourceStreams::ParseCommonSourceMeshStreams(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    Core::Assets::AssetArena& assetArena,
    Core::Alloc::ScratchArena& scratchArena
){
    SourceMeshStreams streams(assetArena, scratchArena);
    if(!MeshCookMetadata::FillMetadataFloatListField<Float3U, 3u>(nwbFilePath, asset, metaKind, s_MeshPositionsFieldNameView, scratchArena, streams.positions))
        return MakeUnexpected(Failure{});
    if(!MeshCookMetadata::FillMetadataFloatListField<Float3U, 3u>(nwbFilePath, asset, metaKind, s_MeshNormalsFieldNameView, scratchArena, streams.normals))
        return MakeUnexpected(Failure{});
    if(!MeshCookMetadata::FillMetadataFloatListField<Float4U, 4u>(nwbFilePath, asset, metaKind, s_MeshTangentsFieldNameView, scratchArena, streams.tangents))
        return MakeUnexpected(Failure{});
    if(!MeshCookMetadata::FillMetadataFloatListField<Float2U, 2u>(nwbFilePath, asset, metaKind, s_MeshUv0FieldNameView, scratchArena, streams.uv0))
        return MakeUnexpected(Failure{});
    if(!MeshCookMetadata::FillMetadataFloatListField<Float4U, 4u>(nwbFilePath, asset, metaKind, s_MeshColorsFieldNameView, scratchArena, streams.colors))
        return MakeUnexpected(Failure{});
    if(!ParseSourceVertexRefsInto(nwbFilePath, asset, metaKind, scratchArena, streams.vertexRefs))
        return MakeUnexpected(Failure{});
    if(!MeshCookMetadata::ParseMetadataIndexFieldInto(nwbFilePath, asset, metaKind, scratchArena, streams.indices))
        return MakeUnexpected(Failure{});
    if(!ValidateSourceIndexStream(nwbFilePath, metaKind, streams.indices, streams.vertexRefs.size()))
        return MakeUnexpected(Failure{});
    if(!ValidateSourceVertexRefs(nwbFilePath, metaKind, streams))
        return MakeUnexpected(Failure{});
    return streams;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

