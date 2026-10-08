// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/metascript/parser.h>
#include <global/binary.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Mesh cook metadata field parsing.


template<typename T>
using ScratchVector = Vector<T, Core::Alloc::ScratchArena>;
using ScratchString = AString<Core::Alloc::ScratchArena>;

struct DiscoveredNwbFile{
    explicit DiscoveredNwbFile(Path::Arena& arena)
        : assetRoot(arena)
        , filePath(arena)
    {}

    Path assetRoot;
    ACompactString virtualRoot;
    Path filePath;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MetadataF32ValueFailure{
    enum Enum : u8{
        NotNumeric,
        NonFinite,
        OutOfRange
    };
};

namespace MetadataU32ValueFailure{
    enum Enum : u8{
        NotNumeric,
        NonIntegerOrNegative,
        OutOfRange
    };
};


static constexpr TStringView s_MeshMetaKind = NWB_TEXT("Mesh");
static constexpr AStringView s_MeshMetaText = "Mesh meta";


static constexpr usize s_IndexedLabelBracketReserve = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshCookMetadata final : NoCopy{
public:
    static constexpr AStringView s_IndicesFieldNameView = "indices";
    static constexpr AStringView s_VertexRefsFieldNameView = "vertex_refs";
    static constexpr AStringView s_PositionsFieldNameView = "positions";
    static constexpr AStringView s_NormalsFieldNameView = "normals";
    static constexpr AStringView s_TangentsFieldNameView = "tangents";
    static constexpr AStringView s_Uv0FieldNameView = "uv0";
    static constexpr AStringView s_ColorsFieldNameView = "colors";
    static Expected<DiscoveredNwbFile> BuildDiscoveredNwbFile(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath
    );
    static bool AccumulateFlattenedValueLeafCount(const Core::Metascript::Value& value, usize& inOutCount);
    static Expected<usize> CountFlattenedValueLeaves(const Core::Metascript::Value& value);
    static ScratchString MakeIndexedLabel(Core::Alloc::ScratchArena& arena, const AStringView baseLabel, const usize index);
    static const Core::Metascript::Value* FindRequiredMetadataListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const TStringView metaKind,
    const AStringView fieldName);
    [[nodiscard]] static Expected<f32, MetadataF32ValueFailure::Enum> ValidateMetadataFiniteF32Value(const Core::Metascript::Value& value);
    static void LogMetadataFiniteF32ValueFailure(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const AStringView label,
    const MetadataF32ValueFailure::Enum failure
    );
    template<usize ComponentCount>
    static Expected<Array<f32, ComponentCount>> ParseMetadataF32TupleWithLabel(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView label,
    Core::Alloc::ScratchArena& scratchArena
    );
    template<usize ComponentCount>
    static Expected<Array<f32, ComponentCount>> ParseMetadataF32TupleListElement(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView fieldName,
    const usize elementIndex,
    Core::Alloc::ScratchArena& scratchArena
    );
    template<typename ElementT, usize ComponentCount>
    static Expected<ScratchVector<ElementT>> ParseMetadataFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    const AStringView fieldName,
    Core::Alloc::ScratchArena& scratchArena
    );
    [[nodiscard]] static Expected<u32, MetadataU32ValueFailure::Enum> ValidateMetadataU32Value(const Core::Metascript::Value& value);
    static void LogMetadataU32ValueFailure(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const AStringView label,
    const MetadataU32ValueFailure::Enum failure
    );
    [[nodiscard]] static Expected<u32, MetadataU32ValueFailure::Enum> ParseMetadataU32Value(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView label
    );
    template<typename IndexVectorT>
    static bool FillMetadataIndexRecursive(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView label,
    IndexVectorT& outIndices,
    Core::Alloc::ScratchArena& scratchArena
    );
    static Expected<Core::Assets::AssetVector<u32>> ParseMetadataIndexField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
    );


public:
    MeshCookMetadata() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize ComponentCount>
Expected<Array<f32, ComponentCount>> MeshCookMetadata::ParseMetadataF32TupleWithLabel(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView label,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!value.isList() || value.asList().size() != ComponentCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a {}-component list")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
            , ComponentCount
        );
        return MakeUnexpected(Failure{});
    }

    Array<f32, ComponentCount> values{};
    const auto& list = value.asList();
    for(usize i = 0u; i < ComponentCount; ++i){
        const auto parsedValue = ValidateMetadataFiniteF32Value(list[i]);
        if(parsedValue){
            values[i] = *parsedValue;
            continue;
        }

        const ScratchString componentLabel = MakeIndexedLabel(scratchArena, label, i);
        LogMetadataFiniteF32ValueFailure(nwbFilePath, metaKind, componentLabel, parsedValue.error());
        return MakeUnexpected(Failure{});
    }
    return values;
}


template<usize ComponentCount>
Expected<Array<f32, ComponentCount>> MeshCookMetadata::ParseMetadataF32TupleListElement(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView fieldName,
    const usize elementIndex,
    Core::Alloc::ScratchArena& scratchArena
){
    const ScratchString label = MakeIndexedLabel(scratchArena, fieldName, elementIndex);
    return ParseMetadataF32TupleWithLabel<ComponentCount>(nwbFilePath, value, metaKind, label, scratchArena);
}


template<typename ElementT, usize ComponentCount>
Expected<ScratchVector<ElementT>> MeshCookMetadata::ParseMetadataFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    const AStringView fieldName,
    Core::Alloc::ScratchArena& scratchArena
){
    ScratchVector<ElementT> values(scratchArena);

    const Core::Metascript::Value* field = FindRequiredMetadataListField(nwbFilePath, asset, metaKind, fieldName);
    if(!field)
        return MakeUnexpected(Failure{});

    const auto& list = field->asList();
    values.reserve(list.size());
    for(usize i = 0u; i < list.size(); ++i){
        const auto tuple = ParseMetadataF32TupleListElement<ComponentCount>(nwbFilePath, list[i], metaKind, fieldName, i, scratchArena);
        if(!tuple){
            return MakeUnexpected(Failure{});
        }

        ElementT element;
        element.x = (*tuple)[0];
        element.y = (*tuple)[1];
        if constexpr(ComponentCount >= 3u)
            element.z = (*tuple)[2];
        if constexpr(ComponentCount >= 4u)
            element.w = (*tuple)[3];
        values.push_back(element);
    }

    if(values.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must not be empty")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }

    return values;
}


template<typename IndexVectorT>
bool MeshCookMetadata::FillMetadataIndexRecursive(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView label,
    IndexVectorT& outIndices,
    Core::Alloc::ScratchArena& scratchArena
){
    if(value.isList()){
        const auto& list = value.asList();
        for(usize i = 0u; i < list.size(); ++i){
            const ScratchString childLabel = MakeIndexedLabel(scratchArena, label, i);
            if(!FillMetadataIndexRecursive(nwbFilePath, list[i], metaKind, childLabel, outIndices, scratchArena))
                return false;
        }
        return true;
    }

    const auto index = ParseMetadataU32Value(nwbFilePath, value, metaKind, label);
    if(!index)
        return false;

    using IndexValue = typename IndexVectorT::value_type;
    outIndices.push_back(static_cast<IndexValue>(*index));
    return true;
}


inline Expected<Core::Assets::AssetVector<u32>> MeshCookMetadata::ParseMetadataIndexField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    Core::Assets::AssetVector<u32> indices(arena);

    const Core::Metascript::Value* field = FindRequiredMetadataListField(nwbFilePath, asset, metaKind, MeshCookMetadata::s_IndicesFieldNameView);
    if(!field)
        return MakeUnexpected(Failure{});

    const auto indexCount = CountFlattenedValueLeaves(*field);
    if(!indexCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' scalar count overflows")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }

    indices.reserve(*indexCount);
    if(!FillMetadataIndexRecursive(nwbFilePath, *field, metaKind, MeshCookMetadata::s_IndicesFieldNameView, indices, scratchArena)){
        return MakeUnexpected(Failure{});
    }
    NWB_ASSERT(indices.size() == *indexCount);
    if(indices.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{} meta '{}': 'indices' must not be empty"),
            metaKind,
            PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }
    return indices;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

