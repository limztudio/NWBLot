// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_metadata.h"
#include "mesh_index_metadata_parser.h"

#include <core/alloc/scratch.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/metadata_fields.h>
#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceMeshStreams{
    ScratchVector<Float3U> positions;
    ScratchVector<Float3U> normals;
    ScratchVector<Float4U> tangents;
    ScratchVector<Float2U> uv0;
    ScratchVector<Float4U> colors;
    ScratchVector<MeshVertexRef> vertexRefs;
    Core::Assets::AssetVector<u32> indices;


public:
    SourceMeshStreams(Core::Assets::AssetArena& assetArena, Core::Alloc::ScratchArena& scratchArena)noexcept
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


namespace MeshCookSourceStreams{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VertexRefVectorT, typename PathArenaT>
b8 ParseSourceVertexRefsInto(
    const ::Path<PathArenaT>& nwbFilePath,
    const Core::Metascript::Value& asset,
    TStringView metaKind,
    Core::Alloc::ScratchArena& scratchArena,
    VertexRefVectorT& outVertexRefs
);
template<typename PathArenaT>
b8 ValidateSourceStreamIndex(const ::Path<PathArenaT>& nwbFilePath, TStringView metaKind, AStringView streamName, u32 index, usize streamCount);
using NWB::Impl::ValidateSourceStreamIndexWithContext;
template<typename PathArenaT>
b8 ValidateSourceIndexStream(const ::Path<PathArenaT>& nwbFilePath, TStringView metaKind, Span<const u32> indices, usize vertexRefCount);
template<typename VertexRefT, typename PathArenaT>
b8 ValidateSourceVertexRefStreams(
    const ::Path<PathArenaT>& nwbFilePath,
    TStringView metaKind,
    Span<const VertexRefT> vertexRefs,
    usize positionCount,
    usize normalCount,
    usize tangentCount,
    usize uv0Count,
    usize colorCount
);
#if defined(NWB_COOK)
inline Expected<SourceMeshStreams> ParseCommonSourceMeshStreams(
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
    if(!MeshIndexMetadataParser::Parse(nwbFilePath, asset, metaKind, scratchArena, streams.indices))
        return MakeUnexpected(Failure{});
    if(!ValidateSourceIndexStream(nwbFilePath, metaKind, streams.indices, streams.vertexRefs.size()))
        return MakeUnexpected(Failure{});
    if(!ValidateSourceVertexRefStreams(
        nwbFilePath,
        metaKind,
        Span<const MeshVertexRef>(streams.vertexRefs.data(), streams.vertexRefs.size()),
        streams.positions.size(),
        streams.normals.size(),
        streams.tangents.size(),
        streams.uv0.size(),
        streams.colors.size()
    ))
        return MakeUnexpected(Failure{});
    return streams;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VertexRefVectorT, typename PathArenaT>
b8 MeshCookSourceStreams::ParseSourceVertexRefsInto(
    const ::Path<PathArenaT>& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    Core::Alloc::ScratchArena& scratchArena,
    VertexRefVectorT& outVertexRefs
){
    outVertexRefs.clear();
    const auto field = MeshCookMetadata::FindRequiredMetadataListField(nwbFilePath, asset, metaKind, s_MeshVertexRefsFieldNameView);
    if(!field)
        return false;

    const auto& list = (*field)->asList();
    const usize expectedComponentCount = s_VertexRefComponentCount;
    outVertexRefs.reserve(list.size());
    using VertexRefValue = typename VertexRefVectorT::value_type;
    for(usize vertexRefIndex = 0u; vertexRefIndex < list.size(); ++vertexRefIndex){
        const Core::Metascript::Value& value = list[vertexRefIndex];
        if(!value.isList() || value.asList().size() != expectedComponentCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'vertex_refs[{}]' must contain {} integer stream indices"), metaKind, PathToString<tchar>(nwbFilePath), vertexRefIndex, expectedComponentCount);
            return false;
        }

        VertexRefValue ref;
        const auto& components = value.asList();
        const Array<AStringView, s_VertexRefComponentCount> componentNames = {
            s_VertexRefPositionName,
            s_VertexRefNormalName,
            s_VertexRefTangentName,
            s_VertexRefUvName,
            s_VertexRefColorName,
        };
        const Array<NotNull<u32*>, s_VertexRefComponentCount> componentValues = {
            MakeNotNull(&ref.position),
            MakeNotNull(&ref.normal),
            MakeNotNull(&ref.tangent),
            MakeNotNull(&ref.uv0),
            MakeNotNull(&ref.color),
        };
        for(usize componentIndex = 0u; componentIndex < expectedComponentCount; ++componentIndex){
            const auto component = MeshCookMetadata::ValidateMetadataU32Value(components[componentIndex]);
            if(!component){
                const ScratchString label = MeshCookMetadata::MakeIndexedLabel(s_MeshVertexRefsFieldNameView, vertexRefIndex, scratchArena);
                ScratchString componentLabel{scratchArena};
                componentLabel.reserve(label.size() + componentNames[componentIndex].size() + s_ComponentLabelExtraCharCount);
                componentLabel.append(label.data(), label.size());
                componentLabel += '.';
                componentLabel.append(componentNames[componentIndex].data(), componentNames[componentIndex].size());
                MeshCookMetadata::LogMetadataU32ValueFailure(nwbFilePath, metaKind, componentLabel, component.error());
                return false;
            }
            *componentValues[componentIndex] = *component;
        }
        outVertexRefs.push_back(ref);
    }

    if(outVertexRefs.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'vertex_refs' must not be empty"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }
    return true;
}

template<typename PathArenaT>
b8 MeshCookSourceStreams::ValidateSourceStreamIndex(const ::Path<PathArenaT>& nwbFilePath, const TStringView metaKind, const AStringView streamName, const u32 index, const usize streamCount){
    if(index < streamCount)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': vertex_ref {} index is out of range"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(streamName));
    return false;
}

template<typename PathArenaT>
b8 MeshCookSourceStreams::ValidateSourceIndexStream(const ::Path<PathArenaT>& nwbFilePath, const TStringView metaKind, const Span<const u32> indices, const usize vertexRefCount){
    switch(ValidateSourceIndexStreamValues(indices, vertexRefCount)){
    case SourceIndexStreamFailure::EmptyOrNotMultipleOfThree:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' must contain whole triangles"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    case SourceIndexStreamFailure::IndexOutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' references an out-of-range vertex_ref"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    case SourceIndexStreamFailure::None:
    default:
        return true;
    }
}

template<typename VertexRefT, typename PathArenaT>
b8 MeshCookSourceStreams::ValidateSourceVertexRefStreams(
    const ::Path<PathArenaT>& nwbFilePath,
    const TStringView metaKind,
    const Span<const VertexRefT> vertexRefs,
    const usize positionCount,
    const usize normalCount,
    const usize tangentCount,
    const usize uv0Count,
    const usize colorCount
){
    for(const VertexRefT& ref : vertexRefs){
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, s_VertexRefPositionName, ref.position, positionCount))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, s_VertexRefNormalName, ref.normal, normalCount))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, s_VertexRefTangentName, ref.tangent, tangentCount))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, s_VertexRefUvName, ref.uv0, uv0Count))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, s_VertexRefColorName, ref.color, colorCount))
            return false;
    }
    return true;
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
