// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DiscoveredNwbFile> MeshCookMetadata::BuildDiscoveredNwbFile(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath
){
    DiscoveredNwbFile file(nwbFilePath.arena());
    file.assetRoot = assetRoot;
    file.virtualRoot.clear();
    file.filePath = nwbFilePath;
    if(!file.virtualRoot.assign(virtualRoot)){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh meta '{}': virtual root exceeds ACompactString capacity")
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }
    return file;
}


bool MeshCookMetadata::AccumulateFlattenedValueLeafCount(const Core::Metascript::Value& value, usize& inOutCount){
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


Expected<usize> MeshCookMetadata::CountFlattenedValueLeaves(const Core::Metascript::Value& value){
    usize count = 0u;
    if(!AccumulateFlattenedValueLeafCount(value, count))
        return MakeUnexpected(Failure{});
    return count;
}


ScratchString MeshCookMetadata::MakeIndexedLabel(
    Core::Alloc::ScratchArena& arena,
    const AStringView baseLabel,
    const usize index
){
    char indexBuffer[TextDetail::s_DecimalTextBufferBytes] = {};
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


const Core::Metascript::Value* MeshCookMetadata::FindRequiredMetadataListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const TStringView metaKind,
    const AStringView fieldName
){
    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(field && field->isList())
        return field;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': field '{}' must be a list")
        , metaKind
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(fieldName)
    );
    return nullptr;
}


Expected<f32, MetadataF32ValueFailure::Enum> MeshCookMetadata::ValidateMetadataFiniteF32Value(
    const Core::Metascript::Value& value
){
    if(!value.isNumeric())
        return Unexpected<MetadataF32ValueFailure::Enum>(MetadataF32ValueFailure::NotNumeric);

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue))
        return Unexpected<MetadataF32ValueFailure::Enum>(MetadataF32ValueFailure::NonFinite);
    if(numericValue < static_cast<f64>(Limit<f32>::s_Min) || numericValue > static_cast<f64>(Limit<f32>::s_Max))
        return Unexpected<MetadataF32ValueFailure::Enum>(MetadataF32ValueFailure::OutOfRange);

    return static_cast<f32>(numericValue);
}


void MeshCookMetadata::LogMetadataFiniteF32ValueFailure(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const AStringView label,
    const MetadataF32ValueFailure::Enum failure
){
    switch(failure){
    case MetadataF32ValueFailure::NotNumeric:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only numeric values")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    case MetadataF32ValueFailure::NonFinite:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only finite numeric values")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    case MetadataF32ValueFailure::OutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a value outside the f32 range")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    default:
        return;
    }
}


Expected<u32, MetadataU32ValueFailure::Enum> MeshCookMetadata::ValidateMetadataU32Value(const Core::Metascript::Value& value){
    if(!value.isNumeric())
        return Unexpected<MetadataU32ValueFailure::Enum>(MetadataU32ValueFailure::NotNumeric);

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue) || numericValue < 0.0 || numericValue != Floor(numericValue))
        return Unexpected<MetadataU32ValueFailure::Enum>(MetadataU32ValueFailure::NonIntegerOrNegative);
    if(numericValue > static_cast<f64>(Limit<u32>::s_Max))
        return Unexpected<MetadataU32ValueFailure::Enum>(MetadataU32ValueFailure::OutOfRange);

    return static_cast<u32>(numericValue);
}


void MeshCookMetadata::LogMetadataU32ValueFailure(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const AStringView label,
    const MetadataU32ValueFailure::Enum failure
){
    switch(failure){
    case MetadataU32ValueFailure::NotNumeric:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only integer values")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    case MetadataU32ValueFailure::NonIntegerOrNegative:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a non-integer or negative value")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    case MetadataU32ValueFailure::OutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a value that exceeds u32")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    default:
        return;
    }
}


Expected<u32, MetadataU32ValueFailure::Enum> MeshCookMetadata::ParseMetadataU32Value(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const TStringView metaKind,
    const AStringView label
){
    const auto parsedValue = ValidateMetadataU32Value(value);
    if(parsedValue)
        return parsedValue;

    LogMetadataU32ValueFailure(nwbFilePath, metaKind, label, parsedValue.error());
    return parsedValue;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

