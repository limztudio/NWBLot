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


static constexpr NotNull<const tchar*> s_MeshMetaKind = MakeNotNull(NWB_TEXT("Mesh"));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshCookMetadata final : NoCopy{
public:
    static bool BuildDiscoveredNwbFile(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    DiscoveredNwbFile& outFile
    );
    static bool AccumulateFlattenedValueLeafCount(const Core::Metascript::Value& value, usize& inOutCount);
    static bool CountFlattenedValueLeaves(const Core::Metascript::Value& value, usize& outCount);
    static ScratchString MakeIndexedLabel(Core::Alloc::ScratchArena& arena, const AStringView baseLabel, const usize index);
    static const Core::Metascript::Value* FindRequiredMetadataListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const NotNull<const tchar*> metaKind,
    const AStringView fieldName);
    static MetadataF32ValueFailure::Enum ValidateMetadataFiniteF32Value(const Core::Metascript::Value& value, f32& outValue);
    static void LogMetadataFiniteF32ValueFailure(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const AStringView label,
    const MetadataF32ValueFailure::Enum failure
    );
    template<usize ComponentCount>
    static bool ParseMetadataF32TupleWithLabel(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const NotNull<const tchar*> metaKind,
    const AStringView label,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
    );
    template<usize ComponentCount>
    static bool ParseMetadataF32TupleListElement(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const NotNull<const tchar*> metaKind,
    const AStringView fieldName,
    const usize elementIndex,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
    );
    template<typename ElementT, usize ComponentCount, typename ElementVectorT>
    static bool ParseMetadataFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const NotNull<const tchar*> metaKind,
    const AStringView fieldName,
    ElementVectorT& outValues,
    Core::Alloc::ScratchArena& scratchArena
    );
    static MetadataU32ValueFailure::Enum ValidateMetadataU32Value(const Core::Metascript::Value& value, u32& outValue);
    static void LogMetadataU32ValueFailure(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const AStringView label,
    const MetadataU32ValueFailure::Enum failure
    );
    static bool ParseMetadataU32Value(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const NotNull<const tchar*> metaKind,
    const AStringView label,
    u32& outValue
    );
    template<typename IndexVectorT>
    static bool FillMetadataIndexRecursive(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const NotNull<const tchar*> metaKind,
    const AStringView label,
    IndexVectorT& outIndices,
    Core::Alloc::ScratchArena& scratchArena
    );
    template<typename IndexVectorT>
    static bool ParseMetadataIndexField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const NotNull<const tchar*> metaKind,
    IndexVectorT& outIndices,
    Core::Alloc::ScratchArena& scratchArena
    );


public:
    MeshCookMetadata() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize ComponentCount>
bool MeshCookMetadata::ParseMetadataF32TupleWithLabel(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const NotNull<const tchar*> metaKind,
    const AStringView label,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
){
    if(!value.isList() || value.asList().size() != ComponentCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a {}-component list")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
            , ComponentCount
        );
        return false;
    }

    const auto& list = value.asList();
    for(usize i = 0; i < ComponentCount; ++i){
        const MetadataF32ValueFailure::Enum failure = ValidateMetadataFiniteF32Value(list[i], outValues[i]);
        if(failure == MetadataF32ValueFailure::None)
            continue;

        const ScratchString componentLabel = MakeIndexedLabel(scratchArena, label, i);
        LogMetadataFiniteF32ValueFailure(nwbFilePath, metaKind, componentLabel, failure);
        return false;
    }
    return true;
}


template<usize ComponentCount>
bool MeshCookMetadata::ParseMetadataF32TupleListElement(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const NotNull<const tchar*> metaKind,
    const AStringView fieldName,
    const usize elementIndex,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
){
    const ScratchString label = MakeIndexedLabel(scratchArena, fieldName, elementIndex);
    return ParseMetadataF32TupleWithLabel(nwbFilePath, value, metaKind, label, outValues, scratchArena);
}


template<typename ElementT, usize ComponentCount, typename ElementVectorT>
bool MeshCookMetadata::ParseMetadataFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const NotNull<const tchar*> metaKind,
    const AStringView fieldName,
    ElementVectorT& outValues,
    Core::Alloc::ScratchArena& scratchArena
){
    outValues.clear();

    const Core::Metascript::Value* field = FindRequiredMetadataListField(nwbFilePath, asset, metaKind, fieldName);
    if(!field)
        return false;

    const auto& list = field->asList();
    outValues.reserve(list.size());
    for(usize i = 0; i < list.size(); ++i){
        alignas(16) f32 tuple[ComponentCount] = {};
        if(!ParseMetadataF32TupleListElement(nwbFilePath, list[i], metaKind, fieldName, i, tuple, scratchArena)){
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
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must not be empty")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    return true;
}


template<typename IndexVectorT>
bool MeshCookMetadata::FillMetadataIndexRecursive(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const NotNull<const tchar*> metaKind,
    const AStringView label,
    IndexVectorT& outIndices,
    Core::Alloc::ScratchArena& scratchArena
){
    if(value.isList()){
        const auto& list = value.asList();
        for(usize i = 0; i < list.size(); ++i){
            const ScratchString childLabel = MakeIndexedLabel(scratchArena, label, i);
            if(!FillMetadataIndexRecursive(nwbFilePath, list[i], metaKind, childLabel, outIndices, scratchArena))
                return false;
        }
        return true;
    }

    u32 index = 0;
    if(!ParseMetadataU32Value(nwbFilePath, value, metaKind, label, index))
        return false;

    using IndexValue = typename IndexVectorT::value_type;
    outIndices.push_back(static_cast<IndexValue>(index));
    return true;
}


template<typename IndexVectorT>
bool MeshCookMetadata::ParseMetadataIndexField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const NotNull<const tchar*> metaKind,
    IndexVectorT& outIndices,
    Core::Alloc::ScratchArena& scratchArena
){
    outIndices.clear();

    const Core::Metascript::Value* field = FindRequiredMetadataListField(nwbFilePath, asset, metaKind, "indices");
    if(!field)
        return false;

    usize indexCount = 0u;
    if(!CountFlattenedValueLeaves(*field, indexCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' scalar count overflows")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outIndices.reserve(indexCount);
    if(!FillMetadataIndexRecursive(nwbFilePath, *field, metaKind, "indices", outIndices, scratchArena)){
        outIndices.clear();
        return false;
    }
    NWB_ASSERT(outIndices.size() == indexCount);
    if(outIndices.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{} meta '{}': 'indices' must not be empty"),
            metaKind.get(),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

