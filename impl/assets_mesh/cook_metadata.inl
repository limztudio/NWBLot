
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


static bool BuildDiscoveredNwbFile(
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

static bool AccumulateFlattenedValueLeafCount(const Core::Metascript::Value& value, usize& inOutCount){
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

static bool CountFlattenedValueLeaves(const Core::Metascript::Value& value, usize& outCount){
    outCount = 0u;
    return AccumulateFlattenedValueLeafCount(value, outCount);
}

static ScratchString MakeIndexedLabel(Core::Alloc::ScratchArena& arena, const AStringView baseLabel, const usize index){
    char indexBuffer[32] = {};
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

static constexpr const tchar* s_MeshMetaKind = NWB_TEXT("Mesh");

static const Core::Metascript::Value* FindRequiredMetadataListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const tchar* metaKind,
    const AStringView fieldName){
    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(field && field->isList())
        return field;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a list")
        , metaKind
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(fieldName)
    );
    return nullptr;
}

static MetadataF32ValueFailure::Enum ValidateMetadataFiniteF32Value(const Core::Metascript::Value& value, f32& outValue){
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

static void LogMetadataFiniteF32ValueFailure(
    const Path& nwbFilePath,
    const tchar* metaKind,
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

template<usize ComponentCount>
static bool ParseMetadataF32TupleWithLabel(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
    const AStringView label,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
){
    if(!value.isList() || value.asList().size() != ComponentCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a {}-component list")
            , metaKind
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
static bool ParseMetadataF32Tuple(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
    const AStringView label,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
){
    return ParseMetadataF32TupleWithLabel(nwbFilePath, value, metaKind, label, outValues, scratchArena);
}

template<usize ComponentCount>
static bool ParseMetadataF32TupleListElement(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
    const AStringView fieldName,
    const usize elementIndex,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
){
    const ScratchString label = MakeIndexedLabel(scratchArena, fieldName, elementIndex);
    return ParseMetadataF32TupleWithLabel(nwbFilePath, value, metaKind, label, outValues, scratchArena);
}

template<typename ElementT, usize ComponentCount, typename ElementVectorT>
static bool ParseMetadataFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const tchar* metaKind,
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
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    return true;
}

static MetadataU32ValueFailure::Enum ValidateMetadataU32Value(const Core::Metascript::Value& value, u32& outValue){
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

static void LogMetadataU32ValueFailure(
    const Path& nwbFilePath,
    const tchar* metaKind,
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

static bool ParseMetadataU32Value(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
    const AStringView label,
    u32& outValue
){
    const MetadataU32ValueFailure::Enum failure = ValidateMetadataU32Value(value, outValue);
    if(failure == MetadataU32ValueFailure::None)
        return true;

    LogMetadataU32ValueFailure(nwbFilePath, metaKind, label, failure);
    return false;
}

template<typename IndexVectorT>
static bool FillMetadataIndexRecursive(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
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
static bool ParseMetadataIndexField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const tchar* metaKind,
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
            , metaKind
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
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' must not be empty"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

