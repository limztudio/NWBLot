// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_metadata.h"

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


class MeshCookSourceStreams final : NoCopy{
public:
    template<typename VertexRefVectorT, typename PathArenaT>
    static b8 ParseSourceVertexRefsInto(
        const ::Path<PathArenaT>& nwbFilePath,
        const Core::Metascript::Value& asset,
        const TStringView metaKind,
        Core::Alloc::ScratchArena& scratchArena,
        VertexRefVectorT& outVertexRefs
    );
    static Expected<ScratchVector<MeshVertexRef>> ParseSourceVertexRefs(const Path& nwbFilePath, const Core::Metascript::Value& asset, const TStringView metaKind, Core::Alloc::ScratchArena& scratchArena);
    template<typename PathArenaT>
    static b8 ValidateSourceStreamIndex(const ::Path<PathArenaT>& nwbFilePath, const TStringView metaKind, const AStringView streamName, const u32 index, const usize streamCount);
    static b8 ValidateSourceStreamIndexWithContext(AStringView context, AStringView streamName, u32 index, usize streamCount);
    template<typename PathArenaT>
    static b8 ValidateSourceIndexStream(const ::Path<PathArenaT>& nwbFilePath, const TStringView metaKind, Span<const u32> indices, const usize vertexRefCount);
    template<typename VertexRefT, typename PathArenaT>
    static b8 ValidateSourceVertexRefStreams(
        const ::Path<PathArenaT>& nwbFilePath,
        const TStringView metaKind,
        Span<const VertexRefT> vertexRefs,
        const usize positionCount,
        const usize normalCount,
        const usize tangentCount,
        const usize uv0Count,
        const usize colorCount
    );
    static b8 ValidateSourceVertexRefs(const Path& nwbFilePath, const TStringView metaKind, const SourceMeshStreams& streams);
    template<typename SkinInfluenceVectorT, typename PathArenaT>
    static b8 ParseSourceSkinInfluencesInto(
        const ::Path<PathArenaT>& nwbFilePath,
        const Core::Metascript::Value& skinAsset,
        const TStringView metaKind,
        const AStringView ownerPrefix,
        const b8 normalizeWeights,
        SkinInfluenceVectorT& outInfluences
    );
#if defined(NWB_COOK)
    static void CopySourceStreams(const SourceMeshStreams& streams, MeshCookEntry& outEntry);
    static Expected<SourceMeshStreams> ParseCommonSourceMeshStreams(const Path& nwbFilePath, const Core::Metascript::Value& asset, const TStringView metaKind, Core::Assets::AssetArena& assetArena, Core::Alloc::ScratchArena& scratchArena);
#endif


public:
    MeshCookSourceStreams() = delete;
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
    if(indices.empty() || (indices.size() % s_MeshletTriangleIndexCount) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' must contain whole triangles"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }
    for(const u32 index : indices){
        if(index < vertexRefCount)
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' references an out-of-range vertex_ref"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }
    return true;
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

template<typename SkinInfluenceVectorT, typename PathArenaT>
b8 MeshCookSourceStreams::ParseSourceSkinInfluencesInto(
    const ::Path<PathArenaT>& nwbFilePath,
    const Core::Metascript::Value& skinAsset,
    const TStringView metaKind,
    const AStringView ownerPrefix,
    const b8 normalizeWeights,
    SkinInfluenceVectorT& outInfluences
){
    outInfluences.clear();
    const auto field = MeshCookMetadata::FindRequiredMetadataListField(nwbFilePath, skinAsset, metaKind, s_SkinInfluencesField);
    if(!field)
        return false;

    const auto& list = (*field)->asList();
    outInfluences.reserve(list.size());
    for(usize influenceIndex = 0u; influenceIndex < list.size(); ++influenceIndex){
        const Core::Metascript::Value& influenceValue = list[influenceIndex];
        if(!influenceValue.isMap()){
            if(ownerPrefix.empty())
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': influences[{}] must be a map"), metaKind, PathToString<tchar>(nwbFilePath), influenceIndex);
            else
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}.influences[{}]' must be a map"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(ownerPrefix), influenceIndex);
            return false;
        }

        if(normalizeWeights){
            if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, influenceValue, s_SkinInfluenceDiagnosticPrefix, { s_SkinJointsField, s_SkinWeightsField }))
                return false;
        }

        const Core::Metascript::Value* joints = influenceValue.findField(s_SkinJointsField);
        const Core::Metascript::Value* weights = influenceValue.findField(s_SkinWeightsField);
        if(!joints || !weights){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': influences[{}] requires 'joints' and 'weights'"), metaKind, PathToString<tchar>(nwbFilePath), influenceIndex);
            return false;
        }
        if(!joints->isList() || joints->asList().size() != s_SkinInfluenceJointCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a {}-component {} list"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(s_SkinJointsField), s_SkinInfluenceJointCount, ConvertString(s_SkinIntegerListValueKind));
            return false;
        }
        if(!weights->isList() || weights->asList().size() != s_SkinInfluenceJointCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a {}-component {} list"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(s_SkinWeightsField), s_SkinInfluenceJointCount, ConvertString(s_SkinNumericListValueKind));
            return false;
        }

        SkinInfluence4 influence{};
        Float4U parsedWeights{};
        for(usize componentIndex = 0u; componentIndex < s_SkinInfluenceJointCount; ++componentIndex){
            const auto parsedJoint = MeshCookMetadata::ValidateMetadataU32Value(joints->asList()[componentIndex]);
            if(!parsedJoint || *parsedJoint > static_cast<u32>(Limit<u16>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}[{}]' must be a u16 integer"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(s_SkinJointsField), componentIndex);
                return false;
            }
            const auto parsedWeight = MeshCookMetadata::ValidateMetadataFiniteF32Value(weights->asList()[componentIndex]);
            if(!parsedWeight){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}[{}]' is non-finite or outside f32 range"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(s_SkinWeightsField), componentIndex);
                return false;
            }
            influence.joint[componentIndex] = static_cast<u16>(*parsedJoint);
            parsedWeights.raw[componentIndex] = *parsedWeight;
        }

        if(normalizeWeights){
            const SIMDVectorSingle rawWeights = LoadFloat(parsedWeights);
            if(!VectorIsFinite(rawWeights, VectorComponentMask::s_XYZW) || !Vector4GreaterOrEqual(rawWeights, VectorZero())){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': influences[{}].weights must be finite and non-negative"), metaKind, PathToString<tchar>(nwbFilePath), influenceIndex);
                return false;
            }

            const SIMDVectorSingle weightSum = Vector4Dot(rawWeights, s_SIMDOne);
            if(!VectorIsFinite(weightSum, VectorComponentMask::s_XYZW) || !Vector4Greater(weightSum, VectorReplicate(s_SkinValidationEpsilon))){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': influences[{}].weights must contain a positive total"), metaKind, PathToString<tchar>(nwbFilePath), influenceIndex);
                return false;
            }

            StoreFloat(VectorDivide(rawWeights, weightSum), influence.weight);
            if(!ValidSkinInfluenceWeights(influence.weight))
                return false;
        }
        else
            influence.weight = parsedWeights;

        outInfluences.push_back(influence);
    }

    if(outInfluences.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must not be empty"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(s_SkinInfluencesField));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

