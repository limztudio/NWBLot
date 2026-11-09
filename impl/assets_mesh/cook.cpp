// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "skin_cook.h"
#include "cook_metadata.h"
#include <core/alloc/scratch.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/metadata_fields.h>
#include <core/assets/paths.h>
#include "meshlet_ref_encode.h"
#include "cook_meshlets.h"
#include "binary_payload_io.h"
#include "meshlet_triangle_indices.h"
#include <impl/assets_mesh/payload_validation.adhd.h>
#include <impl/assets_mesh/mesh_binary_codec.adhd.h>
#include <impl/assets_mesh/skin_binary_codec.adhd.h>
#include <impl/assets_mesh/cook_stream_reorder.adhd.h>
#include <impl/assets_mesh/cook_solid_topology.adhd.h>
#include <impl/assets_mesh/cook_meshlet_scoring.adhd.h>
#include <impl/assets_mesh/cook_meshlet_metrics.adhd.h>
#include <impl/assets_skeleton/cook_matrix.h>
#include <core/assets/binary_payload_io.h>
#include <global/algorithm.h>
#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshIndexMetadataParser final{
private:
    [[nodiscard]] static b8 AccumulateLeaves(const Core::Metascript::Value& value, usize& inOutCount)noexcept{
        if(value.isList()){
            for(const Core::Metascript::Value& child : value.asList()){
                if(!AccumulateLeaves(child, inOutCount))
                    return false;
            }
            return true;
        }

        if(inOutCount == Limit<usize>::s_Max)
            return false;

        ++inOutCount;
        return true;
    }

    [[nodiscard]] static Expected<usize> CountLeaves(const Core::Metascript::Value& value)noexcept{
        usize count = 0u;
        if(!AccumulateLeaves(value, count))
            return MakeUnexpected(Failure{});
        return count;
    }

    template<typename IndexVectorT, typename PathArenaT>
    [[nodiscard]] static b8 FillRecursive(
        const ::Path<PathArenaT>& nwbFilePath,
        const Core::Metascript::Value& value,
        const TStringView metaKind,
        const AStringView label,
        Core::Alloc::ScratchArena& scratchArena,
        IndexVectorT& outIndices
    ){
        struct IndexBreadcrumb{
            const IndexBreadcrumb* parent = nullptr;
            usize index = 0u;
        };
        const auto fill = [&](auto&& recurse, const Core::Metascript::Value& current, const IndexBreadcrumb* path) -> b8{
            if(current.isList()){
                const auto& list = current.asList();
                for(usize i = 0u; i < list.size(); ++i){
                    const IndexBreadcrumb child{ path, i };
                    if(!recurse(recurse, list[i], &child))
                        return false;
                }
                return true;
            }

            const auto index = MeshCookMetadata::ValidateMetadataU32Value(current);
            if(!index){
                ScratchString failureLabel{scratchArena};
                const usize maxLabelBytes = failureLabel.max_size();
                usize depth = 0u;
                usize labelBytes = label.size();
                if(labelBytes > maxLabelBytes)
                    throw LengthException("mesh index diagnostic label exceeds string capacity");
                for(const IndexBreadcrumb* component = path; component; component = component->parent){
                    Array<char, s_DecimalTextBufferBytes> digits{};
                    const usize indexBytes = FormatDecimal(component->index, digits).size();
                    if(indexBytes > maxLabelBytes - labelBytes || s_IndexedLabelBracketReserve > maxLabelBytes - labelBytes - indexBytes)
                        throw LengthException("mesh index diagnostic label exceeds string capacity");
                    labelBytes += indexBytes + s_IndexedLabelBracketReserve;
                    ++depth;
                }
                failureLabel.reserve(labelBytes);
                failureLabel.append(label.data(), label.size());
                for(usize remainingDepth = depth; remainingDepth > 0u; --remainingDepth){
                    const IndexBreadcrumb* component = path;
                    for(usize parentCount = 1u; parentCount < remainingDepth; ++parentCount)
                        component = component->parent;
                    Array<char, s_DecimalTextBufferBytes> digits{};
                    const AStringView indexText = FormatDecimal(component->index, digits);
                    failureLabel += '[';
                    failureLabel.append(indexText.data(), indexText.size());
                    failureLabel += ']';
                }
                MeshCookMetadata::LogMetadataU32ValueFailure(nwbFilePath, metaKind, failureLabel, index.error());
                return false;
            }
            using IndexValue = typename IndexVectorT::value_type;
            outIndices.push_back(static_cast<IndexValue>(*index));
            return true;
        };
        return fill(fill, value, nullptr);
    }


public:
    template<typename IndexVectorT, typename PathArenaT>
    [[nodiscard]] static b8 Parse(
        const ::Path<PathArenaT>& nwbFilePath,
        const Core::Metascript::Value& asset,
        const TStringView metaKind,
        Core::Alloc::ScratchArena& scratchArena,
        IndexVectorT& outIndices
    ){
        outIndices.clear();
        const auto field = MeshCookMetadata::FindRequiredMetadataListField(nwbFilePath, asset, metaKind, s_MeshIndicesFieldNameView);
        if(!field)
            return false;

        const auto indexCount = CountLeaves(**field);
        if(!indexCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' scalar count overflows"), metaKind, PathToString<tchar>(nwbFilePath));
            return false;
        }

        outIndices.reserve(*indexCount);
        if(!FillRecursive(nwbFilePath, **field, metaKind, s_MeshIndicesFieldNameView, scratchArena, outIndices))
            return false;
        NWB_ASSERT(outIndices.size() == *indexCount);
        if(outIndices.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' must not be empty"), metaKind, PathToString<tchar>(nwbFilePath));
            return false;
        }
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinInfluenceMetadataParser final{
public:
    template<typename SkinInfluenceVectorT, typename PathArenaT>
    [[nodiscard]] static b8 Parse(
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
                const auto normalizedWeights = TryNormalizeSkinInfluenceWeights(parsedWeights);
                if(!normalizedWeights){
                    if(normalizedWeights.error() == SkinInfluenceWeightFailure::NegativeWeight)
                        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': influences[{}].weights must be finite and non-negative"), metaKind, PathToString<tchar>(nwbFilePath), influenceIndex);
                    else if(normalizedWeights.error() == SkinInfluenceWeightFailure::ZeroTotalWeight)
                        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': influences[{}].weights must contain a positive total"), metaKind, PathToString<tchar>(nwbFilePath), influenceIndex);
                    return false;
                }
                influence.weight = *normalizedWeights;
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
};


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


b8 MeshCookMeshlets::GrowMeshletFromFrontier(
    const AStringView metaKind,
    const AStringView filePathText,
    const usize seedSearchOffset,
    Core::CpuTaskScheduler& cpuScheduler,
    MeshletTrianglePrecompute& trianglePrecompute,
    Core::Assets::AssetVector<u32>& localSourceVertexRefs,
    Core::Assets::AssetVector<u32>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena>& parallelCandidates,
    Core::Assets::AssetVector<u32>& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
){
    while(MeshletPrimitiveCount(meshlet) < s_MeshMaxMeshletTriangles){
        if(frontier.empty()){
            const auto disconnectedCandidate = MeshletCandidateSearch::FindBestDisconnectedMeshletCandidate(trianglePrecompute, seedSearchOffset + 1u, localTriangleIndices, scoreState, meshlet, localSourceVertexRefs, cpuScheduler, parallelCandidates);
            if(!disconnectedCandidate)
                break;

            if(!AddVisitedMeshletTriangle(
                metaKind,
                filePathText,
                disconnectedCandidate->triangleIndex,
                trianglePrecompute.triangles,
                trianglePrecompute.triangleCalculations,
                trianglePrecompute.positionTriangleOffsets,
                trianglePrecompute.positionTriangleIndices,
                trianglePrecompute.visitedTriangles,
                localSourceVertexRefs,
                localTriangleIndices,
                meshlet,
                primitiveIndices,
                scoreState,
                frontier,
                frontierFlags
            ))
                return false;
            continue;
        }

        const auto bestCandidate = FindBestMeshletFrontierCandidateFromPrecompute(
            trianglePrecompute.triangles,
            trianglePrecompute.triangleCalculations,
            frontier,
            localTriangleIndices,
            scoreState,
            meshlet,
            localSourceVertexRefs
        );
        if(!bestCandidate)
            break;

        RemoveMeshletFrontierCandidate(bestCandidate->frontierOffset, frontier, frontierFlags);
        if(!AddVisitedMeshletTriangle(
            metaKind,
            filePathText,
            bestCandidate->triangleIndex,
            trianglePrecompute.triangles,
            trianglePrecompute.triangleCalculations,
            trianglePrecompute.positionTriangleOffsets,
            trianglePrecompute.positionTriangleIndices,
            trianglePrecompute.visitedTriangles,
            localSourceVertexRefs,
            localTriangleIndices,
            meshlet,
            primitiveIndices,
            scoreState,
            frontier,
            frontierFlags
        ))
            return false;
    }

    return true;
}


b8 MeshCookMeshlets::BuildMeshlets(const Path& nwbFilePath, const TStringView metaKind, const Span<const u32> indices, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, MeshCookEntry& entry){
    auto precomputed = MeshletTriangleGeometry::PrecomputeMeshletTriangleData(nwbFilePath, metaKind, indices, entry, scratchArena);
    if(!precomputed)
        return false;
    MeshletTrianglePrecompute& trianglePrecompute = *precomputed;

    MeshletDesc current;
    const AString<Core::Alloc::ScratchArena> filePathText = PathToString<char>(scratchArena, nwbFilePath);
    Core::Assets::AssetArena& assetArena = entry.positions.get_allocator().arena();
    Core::Assets::AssetVector<u32> localSourceVertexRefs(assetArena);
    Core::Assets::AssetVector<u32> localTriangleIndices(assetArena);
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena> parallelCandidates(scratchArena);
    Core::Assets::AssetVector<u32> frontier(assetArena);
    Core::Assets::AssetVector<u8> frontierFlags(assetArena);
    PrepareMeshletBuildOutputStreams(
        indices.size(),
        trianglePrecompute.triangles.size(),
        entry.meshlets,
        entry.meshletBounds,
        entry.meshletPositionStreamRefs,
        entry.meshletAttributeStreamRefs,
        entry.meshletLocalVertexRefs,
        entry.meshletPrimitiveIndices,
        localSourceVertexRefs,
        localTriangleIndices,
        frontier,
        frontierFlags
    );
    parallelCandidates.reserve((static_cast<usize>(cpuScheduler.workerThreadCount()) + 1u) * s_MeshletDisconnectedCandidateParallelOversubscription);

    MeshletScoreState scoreState;

    auto resetCurrentMeshlet = [&]()noexcept{
        ResetMeshletBuilderState(entry.meshletPrimitiveIndices.size(), current, localSourceVertexRefs, localTriangleIndices, scoreState);
    };

    auto flushMeshlet = [&]() -> b8{
        if(!FlushBuiltMeshlet(
            s_MeshMetaKindText,
            AStringView(filePathText),
            entry.positions,
            entry.vertexRefs,
            entry.meshletPrimitiveIndices,
            localSourceVertexRefs,
            current,
            entry.meshletPositionStreamRefs,
            entry.meshletAttributeStreamRefs,
            entry.meshletLocalVertexRefs,
            entry.meshlets,
            entry.meshletBounds
        ))
            return false;

        resetCurrentMeshlet();
        return true;
    };

    usize seedSearchOffset = 0u;
    while(const Expected<u32> seedTriangleIndex = TryFindNextUnvisitedMeshletTriangle(trianglePrecompute.visitedTriangles, trianglePrecompute.triangles.size(), seedSearchOffset)){
        seedSearchOffset = *seedTriangleIndex;
        ClearMeshletFrontier(frontier, frontierFlags);
        resetCurrentMeshlet();

        if(!AddVisitedMeshletTriangle(
            s_MeshMetaKindText,
            AStringView(filePathText),
            *seedTriangleIndex,
            trianglePrecompute.triangles,
            trianglePrecompute.triangleCalculations,
            trianglePrecompute.positionTriangleOffsets,
            trianglePrecompute.positionTriangleIndices,
            trianglePrecompute.visitedTriangles,
            localSourceVertexRefs,
            localTriangleIndices,
            current,
            entry.meshletPrimitiveIndices,
            scoreState,
            frontier,
            frontierFlags
        ))
            return false;

        if(!GrowMeshletFromFrontier(s_MeshMetaKindText, AStringView(filePathText), seedSearchOffset, cpuScheduler, trianglePrecompute, localSourceVertexRefs, localTriangleIndices, current, entry.meshletPrimitiveIndices, scoreState, parallelCandidates, frontier, frontierFlags))
            return false;

        ClearMeshletFrontier(frontier, frontierFlags);
        if(!flushMeshlet())
            return false;
    }

    if(entry.meshlets.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet build produced no meshlets"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }

    LogMeshletCookMetrics(
        s_MeshMetaKindText,
        AStringView(filePathText),
        indices,
        entry.positions,
        entry.normals,
        entry.tangents,
        entry.uv0,
        entry.colors,
        entry.vertexRefs,
        entry.meshlets,
        entry.meshletBounds,
        entry.meshletPositionStreamRefs,
        entry.meshletAttributeStreamRefs,
        entry.meshletLocalVertexRefs,
        entry.meshletPrimitiveIndices
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_mesh_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 ParseSourceMeshMeta(const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, MeshCookEntry& outEntry){
    auto streamsResult = MeshCookSourceStreams::ParseCommonSourceMeshStreams(nwbFilePath, asset, s_MeshMetaKind, outEntry.positions.get_allocator().arena(), scratchArena);
    if(!streamsResult)
        return false;

    SourceMeshStreams& streams = *streamsResult;
    CopySourceMeshStreamsToCookStreams(
        streams.positions,
        streams.normals,
        streams.tangents,
        streams.uv0,
        streams.colors,
        streams.vertexRefs,
        outEntry.positions,
        outEntry.normals,
        outEntry.tangents,
        outEntry.uv0,
        outEntry.colors,
        outEntry.vertexRefs
    );
    return MeshCookMeshlets::BuildMeshlets(nwbFilePath, s_MeshMetaKind, streams.indices, cpuScheduler, scratchArena, outEntry);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skin_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Core::Assets::AssetVector<SkeletonJointMatrix>> ParseInverseBindMatrices(const Path& nwbFilePath, const Value& asset, Core::Assets::AssetArena& arena){
    Core::Assets::AssetVector<SkeletonJointMatrix> values(arena);

    const auto matrices = Core::Assets::FindMetadataListField(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, s_SkinInverseBindMatricesField);
    if(!matrices)
        return MakeUnexpected(matrices.error());

    const auto& matrixList = (*matrices)->asList();
    values.reserve(matrixList.size());
    for(usize matrixIndex = 0u; matrixIndex < matrixList.size(); ++matrixIndex){
        const auto matrix = AssetsSkeletonCookDetail::ParseSkeletonJointMatrixValue(nwbFilePath, matrixList[matrixIndex], s_SkinMetaKind, s_SkinInverseBindMatricesField);
        if(!matrix)
            return MakeUnexpected(Failure{});

        if(!IsValidSkinInverseBindMatrix(LoadFloat(*matrix))){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': inverse_bind_matrices[{}] is not a finite invertible affine matrix"), PathToString<tchar>(nwbFilePath), matrixIndex);
            return MakeUnexpected(Failure{});
        }
        values.push_back(*matrix);
    }

    if(values.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}' must not be empty"), PathToString<tchar>(nwbFilePath), ConvertString(s_SkinInverseBindMatricesField));
        return MakeUnexpected(Failure{});
    }
    return values;
}

[[nodiscard]] static b8 ValidateSkinInfluenceJointIndices(const Path& nwbFilePath, const SkinCookEntry& entry){
    const Core::Assets::AssetString nwbFilePathText = PathToString(entry.influences.get_allocator().arena(), nwbFilePath);
    return ValidateSkinCookInfluenceJointIndices(AStringView(nwbFilePathText), entry.inverseBindMatrices.size(), entry.influences);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MeshCookEntry> ParseMeshCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Alloc::ScratchArena& scratchArena
){
    return Core::Assets::ParseCookMetadataDocument<MeshCookEntry>(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        s_MeshMetaText,
        [&nwbFilePath, &arena, &cpuScheduler, &scratchArena](const Name virtualPath, const Core::Metascript::Value& asset){
            return ParseMeshCookMetadata(virtualPath, nwbFilePath, asset, arena, cpuScheduler, scratchArena);
        },
        scratchArena
    );
}

Expected<MeshCookEntry> ParseMeshCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena){
    MeshCookEntry entry(arena);

    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_MeshMetaText))
        return MakeUnexpected(Failure{});

    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_MeshMetaText, entry))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_MeshMetaText, &IsAllowedMeshAssetField))
        return MakeUnexpected(Failure{});
    if(!__hidden_assets_mesh_cook::ParseSourceMeshMeta(nwbFilePath, asset, cpuScheduler, scratchArena, entry))
        return MakeUnexpected(Failure{});
    return entry;
}

Expected<Mesh> BuildMeshAsset(MeshCookEntry& meshEntry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    if(!ReorderMeshStreamsByMeshletTraversal(
        meshEntry.virtualPath,
        meshEntry.meshlets,
        meshEntry.positions,
        meshEntry.normals,
        meshEntry.tangents,
        meshEntry.uv0,
        meshEntry.colors,
        meshEntry.meshletPositionStreamRefs,
        meshEntry.meshletAttributeStreamRefs
    ))
        return MakeUnexpected(Failure{});
    if(!EncodeMeshletRefDeltas(meshEntry.meshletPositionStreamRefs, meshEntry.meshletAttributeStreamRefs, false, [&](const usize meshletIndex, const TStringView reason){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet {} {}"), s_MeshMetaKind, ConvertString(meshEntry.virtualPath.resolvedText()), meshletIndex, reason);
        return false;
    }, meshEntry.meshlets, meshEntry.meshletPositionRefDeltas, meshEntry.meshletAttributeRefDeltas))
        return MakeUnexpected(Failure{});

    const auto triangleIndices = BuildMeshletTriangleIndices(meshEntry.meshlets, meshEntry.meshletLocalVertexRefs, meshEntry.meshletPositionRefDeltas, meshEntry.meshletPrimitiveIndices, meshEntry.positions.size(), scratchArena);
    if(!triangleIndices)
        return MakeUnexpected(Failure{});
    Core::Assets::AssetVector<u32> solidWords(arena);
    if(!BuildSolidTriangleWordsInto(meshEntry.positions, *triangleIndices, solidWords))
        return MakeUnexpected(Failure{});

    Mesh mesh(arena, meshEntry.virtualPath);
    mesh.setPayload(
        Move(meshEntry.positions),
        Move(meshEntry.normals),
        Move(meshEntry.tangents),
        Move(meshEntry.uv0),
        Move(meshEntry.colors),
        Move(meshEntry.meshlets),
        Move(meshEntry.meshletBounds),
        Move(meshEntry.meshletPositionRefDeltas),
        Move(meshEntry.meshletAttributeRefDeltas),
        Move(meshEntry.meshletLocalVertexRefs),
        Move(meshEntry.meshletPrimitiveIndices),
        Move(solidWords)
    );
    if(!mesh.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return mesh;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, MeshBinaryPayload::s_MeshSerializeContext))
        return false;

    Core::Alloc::ScratchArena scratchArena(s_MeshValidatePayloadArena);
    const Mesh& mesh = static_cast<const Mesh&>(asset);
    if(!mesh.validatePayload(scratchArena))
        return false;

    return MeshBinaryCodec().serialize(
        mesh.positionStream(),
        mesh.normalStream(),
        mesh.tangentStream(),
        mesh.uv0Stream(),
        mesh.colorStream(),
        mesh.meshlets(),
        mesh.meshletBounds(),
        mesh.meshletPositionRefDeltas(),
        mesh.meshletAttributeRefDeltas(),
        mesh.meshletLocalVertexRefs(),
        mesh.meshletPrimitiveIndices(),
        mesh.solidTriangleWords(),
        outBinary
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SkinAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, s_SkinSerializeContext))
        return false;

    const Skin& skin = static_cast<const Skin&>(asset);
    if(!skin.validatePayload())
        return false;

    return SkinBinaryCodec().serialize(
        skin.mesh().name().hash(),
        skin.skeleton().name().hash(),
        skin.influences(),
        skin.inverseBindMatrices(),
        outBinary
    );
}

Expected<SkinCookEntry> ParseSkinCookMetadata(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    return Core::Assets::ParseCookMetadataDocument<SkinCookEntry>(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        s_SkinMetaDiagnosticPrefix,
        [&nwbFilePath, &arena](const Name virtualPath, const Core::Metascript::Value& asset){
            return ParseSkinCookMetadata(virtualPath, nwbFilePath, asset, arena);
        },
        scratchArena
    );
}

Expected<SkinCookEntry> ParseSkinCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena){
    using namespace __hidden_skin_cook;

    SkinCookEntry entry(arena);

    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix))
        return MakeUnexpected(Failure{});

    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_SkinMetaDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, &IsAllowedSkinMetadataField))
        return MakeUnexpected(Failure{});
    auto meshResult = Core::Assets::ReadMetadataAssetRefField<Mesh>(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, s_SkinMetaMeshField, true);
    if(!meshResult)
        return MakeUnexpected(Failure{});
    entry.mesh = *meshResult;
    auto skeletonResult = Core::Assets::ReadMetadataAssetRefField<Skeleton>(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, s_SkinMetaSkeletonField, true);
    if(!skeletonResult)
        return MakeUnexpected(Failure{});
    entry.skeleton = *skeletonResult;
    if(!SkinInfluenceMetadataParser::Parse(nwbFilePath, asset, NWB_TEXT("Skin"), {}, true, entry.influences))
        return MakeUnexpected(Failure{});
    auto inverseBindMatricesResult = ParseInverseBindMatrices(nwbFilePath, asset, arena);
    if(!inverseBindMatricesResult)
        return MakeUnexpected(Failure{});
    entry.inverseBindMatrices = Move(*inverseBindMatricesResult);
    if(!ValidateSkinInfluenceJointIndices(nwbFilePath, entry))
        return MakeUnexpected(Failure{});

    return entry;
}

Expected<Skin> BuildSkinAsset(SkinCookEntry& skinEntry, Core::Assets::AssetArena& arena){
    Skin asset(arena, skinEntry.virtualPath);
    asset.setMesh(skinEntry.mesh);
    asset.setSkeleton(skinEntry.skeleton);
    asset.setPayload(Move(skinEntry.influences), Move(skinEntry.inverseBindMatrices));
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
