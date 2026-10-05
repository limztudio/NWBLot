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
        None,
        NotNumeric,
        NonFinite,
        OutOfRange
    };
};

namespace MetadataU32ValueFailure{
    enum Enum : u8{
        None,
        NotNumeric,
        NonIntegerOrNegative,
        OutOfRange
    };
};


static constexpr TStringView s_MeshMetaKind = GLB_TEXT("Mesh");
static constexpr AStringView s_MeshMetaText = "Mesh meta";


static constexpr usize s_MetadataTupleAlignment = 16u;
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
    static bool buildDiscoveredNwbFile(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    DiscoveredNwbFile& outFile
    );
    static bool accumulateFlattenedValueLeafCount(const Core::Metascript::Value& value, usize& inOutCount);
    static bool countFlattenedValueLeaves(const Core::Metascript::Value& value, usize& outCount);
    static ScratchString makeIndexedLabel(Core::Alloc::ScratchArena& arena, const AStringView baseLabel, const usize index);
    static const Core::Metascript::Value* findRequiredMetadataListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const TStringView metaKind,
    const AStringView fieldName);
    static MetadataF32ValueFailure::Enum validateMetadataFiniteF32Value(const Core::Metascript::Value& value, f32& outValue);
    static void logMetadataFiniteF32ValueFailure(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const AStringView label,
    const MetadataF32ValueFailure::Enum failure
    );
    template<usize ComponentCount>
    static bool parseMetadataF32TupleWithLabel(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView label,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
    );
    template<usize ComponentCount>
    static bool parseMetadataF32TupleListElement(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView fieldName,
    const usize elementIndex,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
    );
    template<typename ElementT, usize ComponentCount, typename ElementVectorT>
    static bool parseMetadataFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    const AStringView fieldName,
    ElementVectorT& outValues,
    Core::Alloc::ScratchArena& scratchArena
    );
    static MetadataU32ValueFailure::Enum validateMetadataU32Value(const Core::Metascript::Value& value, u32& outValue);
    static void logMetadataU32ValueFailure(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const AStringView label,
    const MetadataU32ValueFailure::Enum failure
    );
    static bool parseMetadataU32Value(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView label,
    u32& outValue
    );
    template<typename IndexVectorT>
    static bool fillMetadataIndexRecursive(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView label,
    IndexVectorT& outIndices,
    Core::Alloc::ScratchArena& scratchArena
    );
    template<typename IndexVectorT>
    static bool parseMetadataIndexField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    IndexVectorT& outIndices,
    Core::Alloc::ScratchArena& scratchArena
    );


public:
    MeshCookMetadata() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize ComponentCount>
bool MeshCookMetadata::parseMetadataF32TupleWithLabel(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView label,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
){
    if(!value.isList() || value.asList().size() != ComponentCount){
        NWB_LOGGER_ERROR(GLB_TEXT("{} meta '{}': '{}' must be a {}-component list")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
            , ComponentCount
        );
        return false;
    }

    const auto& list = value.asList();
    for(usize i = 0u; i < ComponentCount; ++i){
        const MetadataF32ValueFailure::Enum failure = validateMetadataFiniteF32Value(list[i], outValues[i]);
        if(failure == MetadataF32ValueFailure::None)
            continue;

        const ScratchString componentLabel = makeIndexedLabel(scratchArena, label, i);
        logMetadataFiniteF32ValueFailure(nwbFilePath, metaKind, componentLabel, failure);
        return false;
    }
    return true;
}


template<usize ComponentCount>
bool MeshCookMetadata::parseMetadataF32TupleListElement(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView fieldName,
    const usize elementIndex,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
){
    const ScratchString label = makeIndexedLabel(scratchArena, fieldName, elementIndex);
    return parseMetadataF32TupleWithLabel(nwbFilePath, value, metaKind, label, outValues, scratchArena);
}


template<typename ElementT, usize ComponentCount, typename ElementVectorT>
bool MeshCookMetadata::parseMetadataFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    const AStringView fieldName,
    ElementVectorT& outValues,
    Core::Alloc::ScratchArena& scratchArena
){
    outValues.clear();

    const Core::Metascript::Value* field = findRequiredMetadataListField(nwbFilePath, asset, metaKind, fieldName);
    if(!field)
        return false;

    const auto& list = field->asList();
    outValues.reserve(list.size());
    for(usize i = 0u; i < list.size(); ++i){
        alignas(s_MetadataTupleAlignment) f32 tuple[ComponentCount] = {};
        if(!parseMetadataF32TupleListElement(nwbFilePath, list[i], metaKind, fieldName, i, tuple, scratchArena)){
            outValues.clear();
            return false;
        }

        ElementT element;
        element.x = tuple[0];
        element.y = tuple[1];
        if constexpr(ComponentCount >= 3u)
            element.z = tuple[2];
        if constexpr(ComponentCount >= 4u)
            element.w = tuple[3];
        outValues.push_back(element);
    }

    if(outValues.empty()){
        NWB_LOGGER_ERROR(GLB_TEXT("{} meta '{}': '{}' must not be empty")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    return true;
}


template<typename IndexVectorT>
bool MeshCookMetadata::fillMetadataIndexRecursive(
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
            const ScratchString childLabel = makeIndexedLabel(scratchArena, label, i);
            if(!fillMetadataIndexRecursive(nwbFilePath, list[i], metaKind, childLabel, outIndices, scratchArena))
                return false;
        }
        return true;
    }

    u32 index = 0;
    if(!parseMetadataU32Value(nwbFilePath, value, metaKind, label, index))
        return false;

    using IndexValue = typename IndexVectorT::value_type;
    outIndices.push_back(static_cast<IndexValue>(index));
    return true;
}


template<typename IndexVectorT>
bool MeshCookMetadata::parseMetadataIndexField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    IndexVectorT& outIndices,
    Core::Alloc::ScratchArena& scratchArena
){
    outIndices.clear();

    const Core::Metascript::Value* field = findRequiredMetadataListField(nwbFilePath, asset, metaKind, MeshCookMetadata::s_IndicesFieldNameView);
    if(!field)
        return false;

    usize indexCount = 0u;
    if(!countFlattenedValueLeaves(*field, indexCount)){
        NWB_LOGGER_ERROR(GLB_TEXT("{} meta '{}': 'indices' scalar count overflows")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outIndices.reserve(indexCount);
    if(!fillMetadataIndexRecursive(nwbFilePath, *field, metaKind, MeshCookMetadata::s_IndicesFieldNameView, outIndices, scratchArena)){
        outIndices.clear();
        return false;
    }
    GLB_ASSERT(outIndices.size() == indexCount);
    if(outIndices.empty()){
        NWB_LOGGER_ERROR(
            GLB_TEXT("{} meta '{}': 'indices' must not be empty"),
            metaKind,
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

