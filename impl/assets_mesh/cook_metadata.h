// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"
#include "cook.h"
#include <impl/assets_mesh/cook_types.adhd.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/metascript/parser.h>

#include <global/binary.h>
#include <global/exception.h>
#include <global/simplemath.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Assets::ScratchString;
using Core::Assets::ScratchVector;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_MeshMetaKind = NWB_TEXT("Mesh");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshCookMetadataDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 AccumulateFlattenedValueLeafCount(const Core::Metascript::Value& value, usize& inOutCount)noexcept{
    if(value.isList()){
        for(const Core::Metascript::Value& child : value.asList()){
            if(!AccumulateFlattenedValueLeafCount(child, inOutCount))
                return false;
        }
        return true;
    }

    if(inOutCount == Limit<usize>::s_Max)
        return false;

    ++inOutCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshCookMetadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<usize> CountFlattenedValueLeaves(const Core::Metascript::Value& value)noexcept{
    usize count = 0u;
    if(!MeshCookMetadataDetail::AccumulateFlattenedValueLeafCount(value, count))
        return MakeUnexpected(Failure{});
    return count;
}

[[nodiscard]] inline ScratchString MakeIndexedLabel(const AStringView baseLabel, const usize index, Core::Alloc::ScratchArena& arena){
    Array<char, s_DecimalTextBufferBytes> indexBuffer{};
    const AStringView indexText = FormatDecimal(index, indexBuffer);
    NWB_ASSERT(!indexText.empty());

    ScratchString label{arena};
    label.reserve(baseLabel.size() + indexText.size() + s_IndexedLabelBracketReserve);
    label.append(baseLabel.data(), baseLabel.size());
    label += '[';
    label.append(indexText.data(), indexText.size());
    label += ']';
    return label;
}

template<typename PathArenaT>
[[nodiscard]] Expected<NotNull<const Core::Metascript::Value*>> FindRequiredMetadataListField(
    const ::Path<PathArenaT>& nwbFilePath,
    const Core::Metascript::Value& map,
    TStringView metaKind,
    AStringView fieldName
);

[[nodiscard]] inline Expected<f32, MetadataF32ValueFailure::Enum> ValidateMetadataFiniteF32Value(const Core::Metascript::Value& value)noexcept{
    const b8 isNumeric = value.isNumeric();
    return ValidateMetadataFiniteF32Number(isNumeric, isNumeric ? value.toDouble() : 0.0);
}

template<typename PathArenaT>
void LogMetadataFiniteF32ValueFailure(const ::Path<PathArenaT>& nwbFilePath, TStringView metaKind, AStringView label, MetadataF32ValueFailure::Enum failure);
template<usize ComponentCount, typename PathArenaT>
Expected<Array<f32, ComponentCount>> ParseMetadataF32TupleListElement(
    const ::Path<PathArenaT>& nwbFilePath,
    const Core::Metascript::Value& value,
    TStringView metaKind,
    AStringView fieldName,
    usize elementIndex,
    Core::Alloc::ScratchArena& scratchArena
);
template<typename ElementT, usize ComponentCount, typename VectorT, typename PathArenaT>
b8 FillMetadataFloatListField(
    const ::Path<PathArenaT>& nwbFilePath,
    const Core::Metascript::Value& asset,
    TStringView metaKind,
    AStringView fieldName,
    Core::Alloc::ScratchArena& scratchArena,
    VectorT& outValues
);

[[nodiscard]] inline Expected<u32, MetadataU32ValueFailure::Enum> ValidateMetadataU32Value(const Core::Metascript::Value& value)noexcept{
    const b8 isNumeric = value.isNumeric();
    return ValidateMetadataU32Number(isNumeric, isNumeric ? value.toDouble() : 0.0);
}
template<typename PathArenaT>
void LogMetadataU32ValueFailure(const ::Path<PathArenaT>& nwbFilePath, TStringView metaKind, AStringView label, MetadataU32ValueFailure::Enum failure);
template<typename IndexVectorT, typename PathArenaT>
b8 FillMetadataIndexRecursive(
    const ::Path<PathArenaT>& nwbFilePath,
    const Core::Metascript::Value& value,
    TStringView metaKind,
    AStringView label,
    Core::Alloc::ScratchArena& scratchArena,
    IndexVectorT& outIndices
);
template<typename IndexVectorT, typename PathArenaT>
b8 ParseMetadataIndexFieldInto(
    const ::Path<PathArenaT>& nwbFilePath,
    const Core::Metascript::Value& asset,
    TStringView metaKind,
    Core::Alloc::ScratchArena& scratchArena,
    IndexVectorT& outIndices
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PathArenaT>
Expected<NotNull<const Core::Metascript::Value*>> MeshCookMetadata::FindRequiredMetadataListField(
    const ::Path<PathArenaT>& nwbFilePath,
    const Core::Metascript::Value& map,
    const TStringView metaKind,
    const AStringView fieldName
){
    const Core::Metascript::Value* field = map.findField(fieldName);
    if(field && field->isList())
        return MakeNotNull(field);

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': field '{}' must be a list"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
    return MakeUnexpected(Failure{});
}

template<typename PathArenaT>
void MeshCookMetadata::LogMetadataFiniteF32ValueFailure(const ::Path<PathArenaT>& nwbFilePath, const TStringView metaKind, const AStringView label, const MetadataF32ValueFailure::Enum failure){
    switch(failure){
    case MetadataF32ValueFailure::NotNumeric:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only numeric values"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(label));
        return;

    case MetadataF32ValueFailure::NonFinite:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only finite numeric values"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(label));
        return;

    case MetadataF32ValueFailure::OutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a value outside the f32 range"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(label));
        return;

    default:
        return;
    }
}

template<usize ComponentCount, typename PathArenaT>
Expected<Array<f32, ComponentCount>> MeshCookMetadata::ParseMetadataF32TupleListElement(
    const ::Path<PathArenaT>& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView fieldName,
    const usize elementIndex,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!value.isList() || value.asList().size() != ComponentCount){
        const ScratchString label = MakeIndexedLabel(fieldName, elementIndex, scratchArena);
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a {}-component list"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(label), ComponentCount);
        return MakeUnexpected(Failure{});
    }

    Array<f32, ComponentCount> values{};
    const auto& list = value.asList();
    for(usize i = 0u; i < ComponentCount; ++i){
        const auto parsedValue = ValidateMetadataFiniteF32Value(list[i]);
        if(!parsedValue){
            const ScratchString label = MakeIndexedLabel(fieldName, elementIndex, scratchArena);
            const ScratchString componentLabel = MakeIndexedLabel(label, i, scratchArena);
            LogMetadataFiniteF32ValueFailure(nwbFilePath, metaKind, componentLabel, parsedValue.error());
            return MakeUnexpected(Failure{});
        }
        values[i] = *parsedValue;
    }
    return values;
}

template<typename ElementT, usize ComponentCount, typename VectorT, typename PathArenaT>
b8 MeshCookMetadata::FillMetadataFloatListField(
    const ::Path<PathArenaT>& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    const AStringView fieldName,
    Core::Alloc::ScratchArena& scratchArena,
    VectorT& outValues
){
    outValues.clear();
    const auto field = FindRequiredMetadataListField(nwbFilePath, asset, metaKind, fieldName);
    if(!field)
        return false;

    const auto& list = (*field)->asList();
    outValues.reserve(list.size());
    for(usize i = 0u; i < list.size(); ++i){
        const auto tuple = ParseMetadataF32TupleListElement<ComponentCount>(nwbFilePath, list[i], metaKind, fieldName, i, scratchArena);
        if(!tuple)
            return false;

        ElementT element;
        element.x = (*tuple)[0];
        element.y = (*tuple)[1];
        if constexpr(ComponentCount >= 3u)
            element.z = (*tuple)[2];
        if constexpr(ComponentCount >= 4u)
            element.w = (*tuple)[3];
        outValues.push_back(element);
    }

    if(outValues.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must not be empty"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return false;
    }
    return true;
}

template<typename PathArenaT>
void MeshCookMetadata::LogMetadataU32ValueFailure(const ::Path<PathArenaT>& nwbFilePath, const TStringView metaKind, const AStringView label, const MetadataU32ValueFailure::Enum failure){
    switch(failure){
    case MetadataU32ValueFailure::NotNumeric:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only integer values"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(label));
        return;

    case MetadataU32ValueFailure::NonIntegerOrNegative:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a non-integer or negative value"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(label));
        return;

    case MetadataU32ValueFailure::OutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a value that exceeds u32"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(label));
        return;

    default:
        return;
    }
}

template<typename IndexVectorT, typename PathArenaT>
b8 MeshCookMetadata::FillMetadataIndexRecursive(
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

        const auto index = ValidateMetadataU32Value(current);
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
            LogMetadataU32ValueFailure(nwbFilePath, metaKind, failureLabel, index.error());
            return false;
        }
        using IndexValue = typename IndexVectorT::value_type;
        outIndices.push_back(static_cast<IndexValue>(*index));
        return true;
    };
    return fill(fill, value, nullptr);
}

template<typename IndexVectorT, typename PathArenaT>
b8 MeshCookMetadata::ParseMetadataIndexFieldInto(
    const ::Path<PathArenaT>& nwbFilePath,
    const Core::Metascript::Value& asset,
    const TStringView metaKind,
    Core::Alloc::ScratchArena& scratchArena,
    IndexVectorT& outIndices
){
    outIndices.clear();
    const auto field = FindRequiredMetadataListField(nwbFilePath, asset, metaKind, s_MeshIndicesFieldNameView);
    if(!field)
        return false;

    const auto indexCount = CountFlattenedValueLeaves(**field);
    if(!indexCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' scalar count overflows"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }

    outIndices.reserve(*indexCount);
    if(!FillMetadataIndexRecursive(nwbFilePath, **field, metaKind, s_MeshIndicesFieldNameView, scratchArena, outIndices))
        return false;
    NWB_ASSERT(outIndices.size() == *indexCount);
    if(outIndices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' must not be empty"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }
    return true;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

