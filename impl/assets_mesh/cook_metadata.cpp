// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshCookMetadata::BuildDiscoveredNwbFile(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    DiscoveredNwbFile& outFile
){
    outFile.assetRoot = assetRoot;
    outFile.virtualRoot.clear();
    outFile.filePath = nwbFilePath;
    if(!outFile.virtualRoot.assign(virtualRoot)){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh meta '{}': virtual root exceeds ACompactString capacity")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
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


bool MeshCookMetadata::CountFlattenedValueLeaves(const Core::Metascript::Value& value, usize& outCount){
    outCount = 0u;
    return AccumulateFlattenedValueLeafCount(value, outCount);
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
    label.reserve(baseLabel.size() + indexText.size() + 2u);
    label.append(baseLabel.data(), baseLabel.size());
    label += '[';
    label.append(indexText.data(), indexText.size());
    label += ']';
    return label;
}


const Core::Metascript::Value* MeshCookMetadata::FindRequiredMetadataListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const NotNull<const tchar*> metaKind,
    const AStringView fieldName){
    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(field && field->isList())
        return field;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a list")
        , metaKind.get()
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(fieldName)
    );
    return nullptr;
}


MetadataF32ValueFailure::Enum MeshCookMetadata::ValidateMetadataFiniteF32Value(
    const Core::Metascript::Value& value,
    f32& outValue
){
    if(!value.isNumeric())
        return MetadataF32ValueFailure::NotNumeric;

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue))
        return MetadataF32ValueFailure::NonFinite;
    if(numericValue < static_cast<f64>(Limit<f32>::s_Min) || numericValue > static_cast<f64>(Limit<f32>::s_Max))
        return MetadataF32ValueFailure::OutOfRange;

    outValue = static_cast<f32>(numericValue);
    return MetadataF32ValueFailure::None;
}


void MeshCookMetadata::LogMetadataFiniteF32ValueFailure(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const AStringView label,
    const MetadataF32ValueFailure::Enum failure
){
    switch(failure){
    case MetadataF32ValueFailure::NotNumeric:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only numeric values")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    case MetadataF32ValueFailure::NonFinite:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only finite numeric values")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    case MetadataF32ValueFailure::OutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a value outside the f32 range")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    default:
        return;
    }
}


MetadataU32ValueFailure::Enum MeshCookMetadata::ValidateMetadataU32Value(const Core::Metascript::Value& value, u32& outValue){
    if(!value.isNumeric())
        return MetadataU32ValueFailure::NotNumeric;

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue) || numericValue < 0.0 || numericValue != Floor(numericValue))
        return MetadataU32ValueFailure::NonIntegerOrNegative;
    if(numericValue > static_cast<f64>(Limit<u32>::s_Max))
        return MetadataU32ValueFailure::OutOfRange;

    outValue = static_cast<u32>(numericValue);
    return MetadataU32ValueFailure::None;
}


void MeshCookMetadata::LogMetadataU32ValueFailure(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const AStringView label,
    const MetadataU32ValueFailure::Enum failure
){
    switch(failure){
    case MetadataU32ValueFailure::NotNumeric:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only integer values")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    case MetadataU32ValueFailure::NonIntegerOrNegative:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a non-integer or negative value")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    case MetadataU32ValueFailure::OutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a value that exceeds u32")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    default:
        return;
    }
}


bool MeshCookMetadata::ParseMetadataU32Value(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const NotNull<const tchar*> metaKind,
    const AStringView label,
    u32& outValue
){
    const MetadataU32ValueFailure::Enum failure = ValidateMetadataU32Value(value, outValue);
    if(failure == MetadataU32ValueFailure::None)
        return true;

    LogMetadataU32ValueFailure(nwbFilePath, metaKind, label, failure);
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

