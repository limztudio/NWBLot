// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_bind{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// material bind typed layout helpers
struct MaterialTypedValueData{
    UInt4 meta = {};
    UInt4 data = {};
};
static_assert(
    sizeof(MaterialTypedValueData) == sizeof(u32) * 8u,
    "MaterialTypedValueData layout must stay stable for typed value parsing"
);
static_assert(alignof(MaterialTypedValueData) >= alignof(UInt4), "MaterialTypedValueData must stay SIMD-aligned");
static_assert(IsTriviallyCopyable_V<MaterialTypedValueData>, "MaterialTypedValueData must stay cheap to copy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool SplitMaterialParameterCall(const AStringView text, AStringView& outType, AStringView& outArgs){
    const AStringView trimmed = TrimView(text);
    usize openParen = Limit<usize>::s_Max;
    for(usize i = 0; i < trimmed.size(); ++i){
        if(trimmed[i] == '('){
            openParen = i;
            break;
        }
    }
    if(openParen == Limit<usize>::s_Max || trimmed.empty() || trimmed[trimmed.size() - 1u] != ')')
        return false;

    outType = TrimView(trimmed.substr(0u, openParen));
    outArgs = TrimView(trimmed.substr(openParen + 1u, trimmed.size() - openParen - 2u));
    return !outType.empty() && !outArgs.empty();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ReadMaterialParameterToken(const AStringView text, usize& inOutCursor, AStringView& outToken){
    while(inOutCursor < text.size() && (IsAsciiSpace(text[inOutCursor]) || text[inOutCursor] == ','))
        ++inOutCursor;
    if(inOutCursor >= text.size())
        return false;

    const usize begin = inOutCursor;
    while(inOutCursor < text.size() && !IsAsciiSpace(text[inOutCursor]) && text[inOutCursor] != ',')
        ++inOutCursor;

    outToken = TrimView(text.substr(begin, inOutCursor - begin));
    return !outToken.empty();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool SplitMaterialParameterTokens(const AStringView text, AStringView (&outTokens)[4], u32& outTokenCount){
    outTokenCount = 0u;
    usize cursor = 0u;
    AStringView token;
    while(ReadMaterialParameterToken(text, cursor, token)){
        if(outTokenCount >= 4u)
            return false;

        outTokens[outTokenCount] = token;
        ++outTokenCount;
    }

    return outTokenCount > 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialBoolToken(const AStringView token, u32& outValue){
    if(token == AStringView("true") || token == AStringView("1")){
        outValue = 1u;
        return true;
    }
    if(token == AStringView("false") || token == AStringView("0")){
        outValue = 0u;
        return true;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static AStringView StripMaterialNumericSuffix(const AStringView token, const char suffixLower, const char suffixUpper){
    if(token.size() <= 1u)
        return token;

    const char suffix = token[token.size() - 1u];
    return (suffix == suffixLower || suffix == suffixUpper) ? token.substr(0u, token.size() - 1u) : token;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialParameterF32Token(const char* begin, const char* end, f32& outValue){
    f64 parsed = 0.0;
    if(!ParseF64FromChars(begin, end, parsed) || !IsFinite(parsed))
        return false;
    if(parsed < static_cast<f64>(Limit<f32>::s_Min) || parsed > static_cast<f64>(Limit<f32>::s_Max))
        return false;

    outValue = static_cast<f32>(parsed);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialParameterSignedToken(
    const char* begin,
    const char* end,
    const i64 minValue,
    const i64 maxValue,
    const u32 storageMask,
    u32& outValue
){
    i64 parsed = 0;
    if(!ParseI64FromChars(begin, end, parsed))
        return false;
    if(parsed < minValue || parsed > maxValue)
        return false;

    outValue = static_cast<u32>(static_cast<u64>(parsed) & storageMask);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialParameterUnsignedToken(
    const char* begin,
    const char* end,
    const u64 maxValue,
    u32& outValue
){
    u64 parsed = 0u;
    if(!ParseU64FromChars(begin, end, parsed) || parsed > maxValue)
        return false;

    outValue = static_cast<u32>(parsed);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialParameterToken(const AStringView token, const MaterialParameterValueType::Enum type, u32& outValue){
    AStringView numericToken = token;
    if(type == MaterialParameterValueType::Float || type == MaterialParameterValueType::Half)
        numericToken = StripMaterialNumericSuffix(token, 'f', 'F');
    else if(
        type == MaterialParameterValueType::UChar
        || type == MaterialParameterValueType::UShort
        || type == MaterialParameterValueType::UInt
    )
        numericToken = StripMaterialNumericSuffix(token, 'u', 'U');

    const char* begin = numericToken.data();
    const char* end = begin + numericToken.size();

    switch(type){
    case MaterialParameterValueType::Bool:
        return ParseMaterialBoolToken(token, outValue);
    case MaterialParameterValueType::Char:
        return ParseMaterialParameterSignedToken(begin, end, -128, 127, NWB_MATERIAL_TYPED_BYTE_MASK, outValue);
    case MaterialParameterValueType::UChar:
        return ParseMaterialParameterUnsignedToken(begin, end, static_cast<u64>(Limit<u8>::s_Max), outValue);
    case MaterialParameterValueType::Short:
        return ParseMaterialParameterSignedToken(
            begin,
            end,
            static_cast<i64>(Limit<i16>::s_Min),
            static_cast<i64>(Limit<i16>::s_Max),
            NWB_MATERIAL_TYPED_U16_MASK,
            outValue
        );
    case MaterialParameterValueType::UShort:
        return ParseMaterialParameterUnsignedToken(begin, end, static_cast<u64>(Limit<u16>::s_Max), outValue);
    case MaterialParameterValueType::Int:
        return ParseMaterialParameterSignedToken(
            begin,
            end,
            static_cast<i64>(Limit<i32>::s_Min),
            static_cast<i64>(Limit<i32>::s_Max),
            Limit<u32>::s_Max,
            outValue
        );
    case MaterialParameterValueType::UInt:
        return ParseMaterialParameterUnsignedToken(begin, end, static_cast<u64>(Limit<u32>::s_Max), outValue);
    case MaterialParameterValueType::Half:{
        f32 converted = 0.f;
        if(!ParseMaterialParameterF32Token(begin, end, converted))
            return false;

        outValue = static_cast<u32>(ConvertFloatToHalf(converted));
        return true;
    }
    case MaterialParameterValueType::Float:{
        f32 converted = 0.f;
        if(!ParseMaterialParameterF32Token(begin, end, converted))
            return false;
        NWB_MEMCPY(&outValue, sizeof(outValue), &converted, sizeof(converted));
        return true;
    }
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool StoreMaterialTypedValueBytes(
    MaterialTypedValueData& outParameter,
    const usize byteOffset,
    const void* bytes,
    const usize byteSize
){
    if(byteOffset > sizeof(outParameter.data) || byteSize > sizeof(outParameter.data) - byteOffset)
        return false;

    u8* outBytes = reinterpret_cast<u8*>(&outParameter.data);
    NWB_MEMCPY(outBytes + byteOffset, sizeof(outParameter.data) - byteOffset, bytes, byteSize);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueType>
static bool StoreMaterialTypedValueScalar(
    MaterialTypedValueData& outParameter,
    const u32 componentIndex,
    const u32 value
){
    const usize byteOffset = static_cast<usize>(componentIndex) * sizeof(ValueType);
    const ValueType typedValue = static_cast<ValueType>(value);
    return StoreMaterialTypedValueBytes(outParameter, byteOffset, &typedValue, sizeof(typedValue));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool StoreMaterialTypedValueComponent(
    MaterialTypedValueData& outParameter,
    const MaterialParameterValueType::Enum valueType,
    const u32 componentIndex,
    const u32 value
){
    if(componentIndex >= 4u)
        return false;

    switch(valueType){
    case MaterialParameterValueType::Bool:
        return StoreMaterialTypedValueScalar<u8>(outParameter, componentIndex, value != 0u ? 1u : 0u);
    case MaterialParameterValueType::Char:
    case MaterialParameterValueType::UChar:
        return StoreMaterialTypedValueScalar<u8>(outParameter, componentIndex, value);
    case MaterialParameterValueType::Short:
    case MaterialParameterValueType::UShort:
        return StoreMaterialTypedValueScalar<u16>(outParameter, componentIndex, value);
    case MaterialParameterValueType::Half:
        return StoreMaterialTypedValueScalar<Half>(outParameter, componentIndex, value);
    default:
        outParameter.data.raw[componentIndex] = value;
        return true;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool BuildMaterialTypedValueData(
    const ACompactString& key,
    const ACompactString& value,
    MaterialTypedValueData& outParameter
){
    outParameter = {};
    if(!key || !value)
        return false;

    MaterialParameterValueType::Enum valueType = MaterialParameterValueType::None;
    u32 componentCount = 0u;
    const AStringView valueText = TrimView(value.view());
    AStringView argsText;
    AStringView typeText;
    if(!SplitMaterialParameterCall(valueText, typeText, argsText))
        return false;
    if(!ParseMaterialParameterTypeText(typeText, valueType, componentCount))
        return false;

    AStringView tokens[4];
    u32 tokenCount = 0u;
    if(!SplitMaterialParameterTokens(argsText, tokens, tokenCount))
        return false;
    if(tokenCount != componentCount)
        return false;

    for(u32 i = 0; i < tokenCount; ++i){
        u32 parsedValue = 0u;
        if(!ParseMaterialParameterToken(tokens[i], valueType, parsedValue))
            return false;
        if(!StoreMaterialTypedValueComponent(outParameter, valueType, i, parsedValue))
            return false;
    }

    const u64 keyHash = ComputeMaterialBindParameterKeyHash(key.view());
    outParameter.meta.x = static_cast<u32>(keyHash & 0xffffffffull);
    outParameter.meta.y = static_cast<u32>(keyHash >> 32u);
    outParameter.meta.z = static_cast<u32>(valueType);
    outParameter.meta.w = componentCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialLayoutFieldType(
    const AStringView typeText,
    MaterialLayoutFieldType::Enum& outFieldType
){
    outFieldType = MaterialLayoutFieldType::None;

    MaterialParameterValueType::Enum valueType = MaterialParameterValueType::None;
    u32 componentCount = 0u;
    if(!ParseMaterialParameterTypeText(typeText, valueType, componentCount))
        return false;

    outFieldType = MaterialLayoutFieldTypeFromParameterType(valueType, componentCount);
    return IsValidMaterialLayoutFieldType(outFieldType);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialBindBlockClass(
    const MaterialBindStruct& bindStruct,
    MaterialBlockClass::Enum& outBlockClass
){
    outBlockClass = MaterialBlockClass::None;

    const MaterialBindAttribute* constantAttribute = bindStruct.findAttribute(s_MaterialConstantAttribute);
    if(constantAttribute){
        outBlockClass = MaterialBlockClass::MaterialConstant;
        return true;
    }

    const MaterialBindAttribute* mutableAttribute = bindStruct.findAttribute(s_MaterialMutableAttribute);
    if(mutableAttribute){
        outBlockClass = MaterialBlockClass::MaterialMutable;
        return true;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static UInt4U ToMaterialTypedLayoutDefaultValue(const MaterialTypedValueData& parameter){
    UInt4U result = {};
    for(u32 i = 0u; i < 4u; ++i)
        result.raw[i] = parameter.data.raw[i];
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool BuildMaterialTypedLayoutDefaultValue(
    const Name& materialName,
    const MaterialBindInstance& instance,
    const MaterialBindField& bindField,
    const MaterialLayoutFieldType::Enum fieldType,
    UInt4U& outDefaultValue
){
    outDefaultValue = {};

    ACompactString key;
    if(!BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(bindField.name), key)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: field '{}.{}' for '{}' exceeds ACompactString capacity")
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.c_str())
        );
        return false;
    }

    ACompactString defaultText;
    const AStringView defaultArgument = bindField.defaultArgument();
    if(!defaultText.assign(defaultArgument)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default for '{}.{}' in '{}' exceeds ACompactString capacity")
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.c_str())
        );
        return false;
    }

    MaterialTypedValueData defaultParameter;
    if(!BuildMaterialTypedValueData(key, defaultText, defaultParameter)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default '{}' for '{}.{}' in '{}' is invalid")
            , StringConvert(defaultArgument)
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.c_str())
        );
        return false;
    }

    const MaterialLayoutFieldType::Enum defaultFieldType = MaterialLayoutFieldTypeFromParameterType(
        static_cast<MaterialParameterValueType::Enum>(defaultParameter.meta.z),
        defaultParameter.meta.w
    );
    if(defaultFieldType != fieldType){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default '{}' for '{}.{}' in '{}' "
            "does not match field type '{}'")
            , StringConvert(defaultArgument)
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.c_str())
            , StringConvert(bindField.type)
        );
        return false;
    }

    outDefaultValue = ToMaterialTypedLayoutDefaultValue(defaultParameter);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool GetMaterialTypedLayoutFieldBytes(
    const MaterialLayoutFieldType::Enum fieldType,
    const UInt4U& value,
    const u8*& outBytes,
    u32& outByteSize
){
    outByteSize = MaterialLayoutFieldByteSize(fieldType);
    if(outByteSize == 0u || outByteSize > sizeof(value))
        return false;

    outBytes = reinterpret_cast<const u8*>(&value);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool AppendMaterialTypedLayoutFieldBytes(
    Material::TypedBlockByteVector& outBlockBytes,
    const MaterialLayoutFieldType::Enum fieldType,
    const UInt4U& value
){
    const u8* bytes = nullptr;
    u32 fieldByteSize = 0u;
    if(!GetMaterialTypedLayoutFieldBytes(fieldType, value, bytes, fieldByteSize))
        return false;

    const usize byteCount = outBlockBytes.size();
    if(static_cast<usize>(fieldByteSize) > Limit<usize>::s_Max - byteCount)
        return false;

    outBlockBytes.reserve(byteCount + fieldByteSize);
    outBlockBytes.insert(outBlockBytes.end(), bytes, bytes + fieldByteSize);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool PadMaterialTypedLayoutBytesTo(
    Material::TypedBlockByteVector& outBlockBytes,
    const usize targetByteSize
){
    if(targetByteSize < outBlockBytes.size())
        return false;

    if(targetByteSize > outBlockBytes.capacity())
        outBlockBytes.reserve(targetByteSize);
    outBlockBytes.resize(targetByteSize, 0u);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ReserveMaterialBindTypedLayoutVectors(
    const MaterialBindEntry& bindEntry,
    const Name& contextName,
    const ScratchVector<const MaterialBindInstance*>& sortedInstances,
    MaterialBindTypedLayout& outLayout
){
    usize fieldReserveCount = 0u;
    usize byteReserveCount = 0u;
    for(const MaterialBindInstance* instance : sortedInstances){
        const MaterialBindStruct* bindStruct = bindEntry.findStruct(AStringView(instance->type));
        if(!bindStruct)
            continue;

        if(fieldReserveCount > Limit<usize>::s_Max - bindStruct->fields.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' exceeds supported field count for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(contextName.c_str())
            );
            return false;
        }
        fieldReserveCount += bindStruct->fields.size();

        for(const MaterialBindField& bindField : bindStruct->fields){
            usize fieldByteReserve = sizeof(UInt4U);
            MaterialLayoutFieldType::Enum fieldType = MaterialLayoutFieldType::None;
            if(ParseMaterialLayoutFieldType(AStringView(bindField.type), fieldType)){
                const u32 fieldByteSize = MaterialLayoutFieldByteSize(fieldType);
                if(fieldByteSize != 0u)
                    fieldByteReserve = fieldByteSize;
            }

            if(byteReserveCount > Limit<usize>::s_Max - fieldByteReserve){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' exceeds supported byte count for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(contextName.c_str())
                );
                return false;
            }
            byteReserveCount += fieldByteReserve;
        }
    }

    outLayout.typedLayoutBlocks.reserve(sortedInstances.size());
    outLayout.typedLayoutFields.reserve(fieldReserveCount);
    outLayout.typedBlockBytes.reserve(byteReserveCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool BuildSortedMaterialBindInstances(
    const MaterialBindEntry& bindEntry,
    const Name& contextName,
    ScratchVector<const MaterialBindInstance*>& outInstances
){
    outInstances.clear();
    outInstances.reserve(bindEntry.instances.size());
    for(const MaterialBindInstance& instance : bindEntry.instances)
        outInstances.push_back(&instance);
    Sort(outInstances.begin(), outInstances.end(), [](const MaterialBindInstance* lhs, const MaterialBindInstance* rhs){
        return lhs->name < rhs->name;
    });
    if(outInstances.size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' exceeds supported block count for '{}'")
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(contextName.c_str())
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool BuildMaterialBindTypedLayoutBlockLookup(
    const AStringView contextLabel,
    MaterialBindTypedLayout& inOutLayout
){
    inOutLayout.blockLookup.clear();
    inOutLayout.blockLookup.reserve(inOutLayout.typedLayoutBlocks.size());

    usize blockByteBegin = 0u;
    for(usize blockIndex = 0u; blockIndex < inOutLayout.typedLayoutBlocks.size(); ++blockIndex){
        const MaterialTypedLayoutBlock& block = inOutLayout.typedLayoutBlocks[blockIndex];
        if(blockIndex > Limit<u32>::s_Max || blockByteBegin > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: block byte offset exceeds u32 for '{}'")
                , StringConvert(contextLabel)
            );
            return false;
        }

        const MaterialBindTypedLayoutBlockLookupEntry entry{ static_cast<u32>(blockIndex), static_cast<u32>(blockByteBegin) };
        if(!inOutLayout.blockLookup.emplace(block.blockName, entry).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: duplicate block in '{}'"), StringConvert(contextLabel));
            return false;
        }
        if(static_cast<usize>(block.byteSize) > Limit<usize>::s_Max - blockByteBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: block byte size overflows for '{}'")
                , StringConvert(contextLabel)
            );
            return false;
        }

        blockByteBegin += block.byteSize;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool BuildMaterialBindTypedLayoutParameterLookup(
    const AStringView contextLabel,
    MaterialBindTypedLayout& inOutLayout
){
    inOutLayout.parameterLookup.clear();
    inOutLayout.parameterLookup.reserve(inOutLayout.typedLayoutFields.size());

    const MaterialBindEntry& bindEntry = *inOutLayout.bindEntry;
    for(const MaterialBindInstance& instance : bindEntry.instances){
        const MaterialBindStruct* bindStruct = bindEntry.findStruct(AStringView(instance.type));
        if(!bindStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' instance '{}' references unknown "
                "struct type '{}' for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance.name)
                , StringConvert(instance.type)
                , StringConvert(contextLabel)
            );
            return false;
        }

        const auto blockIt = inOutLayout.blockLookup.find(Name(AStringView(instance.name)));
        if(blockIt == inOutLayout.blockLookup.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' instance '{}' has no block for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance.name)
                , StringConvert(contextLabel)
            );
            return false;
        }

        const MaterialBindTypedLayoutBlockLookupEntry& blockEntry = blockIt.value();
        if(blockEntry.blockIndex >= inOutLayout.typedLayoutBlocks.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block lookup is out of range for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(contextLabel)
            );
            return false;
        }

        const MaterialTypedLayoutBlock& block = inOutLayout.typedLayoutBlocks[blockEntry.blockIndex];
        if(block.fieldCount != bindStruct->fields.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' instance '{}' field count mismatch for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance.name)
                , StringConvert(contextLabel)
            );
            return false;
        }

        for(u32 fieldOffset = 0u; fieldOffset < block.fieldCount; ++fieldOffset){
            const usize fieldIndex = static_cast<usize>(block.fieldBegin) + fieldOffset;
            if(fieldIndex >= inOutLayout.typedLayoutFields.size() || fieldIndex > Limit<u32>::s_Max){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' instance '{}' "
                    "field range exceeds layout for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance.name)
                    , StringConvert(contextLabel)
                );
                return false;
            }

            const MaterialBindField& bindField = bindStruct->fields[fieldOffset];
            const MaterialTypedLayoutField& field = inOutLayout.typedLayoutFields[fieldIndex];
            if(field.fieldName != Name(AStringView(bindField.name))){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' "
                    "metadata mismatch for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(bindStruct->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextLabel)
                );
                return false;
            }
            if(field.offset > Limit<u32>::s_Max - blockEntry.byteBegin){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' "
                    "byte offset exceeds u32 for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(bindStruct->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextLabel)
                );
                return false;
            }

            ACompactString parameterName;
            if(!BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(bindField.name), parameterName)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: field '{}.{}' for '{}' exceeds ACompactString capacity")
                    , StringConvert(instance.name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextLabel)
                );
                return false;
            }

            const MaterialBindTypedLayoutParameterLookupEntry entry{
                static_cast<u32>(fieldIndex),
                blockEntry.byteBegin + field.offset
            };
            if(!inOutLayout.parameterLookup.emplace(parameterName, entry).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: duplicate parameter '{}' in '{}'")
                    , StringConvert(parameterName.c_str())
                    , StringConvert(contextLabel)
                );
                return false;
            }
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool WriteMaterialTypedLayoutFieldBytes(
    Material::TypedBlockByteVector& inOutBlockBytes,
    const usize byteOffset,
    const MaterialLayoutFieldType::Enum fieldType,
    const UInt4U& value
){
    const u8* bytes = nullptr;
    u32 fieldByteSize = 0u;
    if(!GetMaterialTypedLayoutFieldBytes(fieldType, value, bytes, fieldByteSize))
        return false;
    if(byteOffset > inOutBlockBytes.size() || static_cast<usize>(fieldByteSize) > inOutBlockBytes.size() - byteOffset)
        return false;

    NWB_MEMCPY(inOutBlockBytes.data() + byteOffset, fieldByteSize, bytes, fieldByteSize);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialTypedLayoutParameterValue(
    const Name& materialName,
    const ACompactString& parameterName,
    const ACompactString& parameterValue,
    const MaterialTypedLayoutField& field,
    UInt4U& outValue
){
    outValue = {};

    MaterialTypedValueData parameter;
    if(!BuildMaterialTypedValueData(parameterName, parameterValue, parameter)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' for '{}' has invalid value '{}'")
            , StringConvert(parameterName.c_str())
            , StringConvert(materialName.c_str())
            , StringConvert(parameterValue.c_str())
        );
        return false;
    }

    const MaterialLayoutFieldType::Enum parameterFieldType = MaterialLayoutFieldTypeFromParameterType(
        static_cast<MaterialParameterValueType::Enum>(parameter.meta.z),
        parameter.meta.w
    );
    if(parameterFieldType != field.fieldType){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' for '{}' does not match interface field type")
            , StringConvert(parameterName.c_str())
            , StringConvert(materialName.c_str())
        );
        return false;
    }

    outValue = ToMaterialTypedLayoutDefaultValue(parameter);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ApplyMaterialBindTypedLayoutParameterValue(
    const MaterialBindTypedLayout& layout,
    const Name& materialName,
    const ACompactString& parameterName,
    const ACompactString& parameterValue,
    Material::TypedBlockByteVector& inOutBlockBytes
){
    const auto parameterIt = layout.parameterLookup.find(parameterName);
    if(parameterIt == layout.parameterLookup.end()){
        AStringView interfacePath = "<unknown>";
        if(layout.bindEntry)
            interfacePath = AStringView(layout.bindEntry->virtualPath);

        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: typed parameter '{}' is not declared "
            "by interface '{}' for '{}'")
            , StringConvert(parameterName.c_str())
            , StringConvert(interfacePath)
            , StringConvert(materialName.c_str())
        );
        return false;
    }
    const MaterialBindTypedLayoutParameterLookupEntry& parameterEntry = parameterIt.value();
    if(parameterEntry.fieldIndex >= layout.typedLayoutFields.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' field index is out of range for '{}'")
            , StringConvert(parameterName.c_str())
            , StringConvert(materialName.c_str())
        );
        return false;
    }

    const MaterialTypedLayoutField& field = layout.typedLayoutFields[parameterEntry.fieldIndex];

    UInt4U typedValue = {};
    if(!ParseMaterialTypedLayoutParameterValue(materialName, parameterName, parameterValue, field, typedValue))
        return false;

    if(!WriteMaterialTypedLayoutFieldBytes(inOutBlockBytes, parameterEntry.byteOffset, field.fieldType, typedValue)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' write exceeds packed layout bytes for '{}'")
            , StringConvert(parameterName.c_str())
            , StringConvert(materialName.c_str())
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildMaterialBindTypedLayoutImpl(
    const MaterialBindEntry& bindEntry,
    const Name& contextName,
    MaterialBindTypedLayout& outLayout,
    ScratchArena& scratchArena
){
    outLayout.reset();
    outLayout.bindEntry = &bindEntry;

    ScratchVector<const MaterialBindInstance*> sortedInstances{ scratchArena };
    if(!BuildSortedMaterialBindInstances(bindEntry, contextName, sortedInstances))
        return false;
    if(!ReserveMaterialBindTypedLayoutVectors(bindEntry, contextName, sortedInstances, outLayout))
        return false;

    for(const MaterialBindInstance* instance : sortedInstances){
        const MaterialBindStruct* bindStruct = bindEntry.findStruct(AStringView(instance->type));
        if(!bindStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' instance '{}' references unknown "
                "struct type '{}' for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance->name)
                , StringConvert(instance->type)
                , StringConvert(contextName.c_str())
            );
            return false;
        }

        MaterialBlockClass::Enum blockClass = MaterialBlockClass::None;
        if(!ParseMaterialBindBlockClass(*bindStruct, blockClass)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' struct '{}' is missing "
                "a material block class for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(bindStruct->name)
                , StringConvert(contextName.c_str())
            );
            return false;
        }

        if(outLayout.typedLayoutFields.size() > Limit<u32>::s_Max || bindStruct->fields.size() > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' exceeds "
                "supported field count for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance->name)
                , StringConvert(contextName.c_str())
            );
            return false;
        }

        const usize blockByteBegin = outLayout.typedBlockBytes.size();

        MaterialTypedLayoutBlock block;
        block.blockName = Name(AStringView(instance->name));
        block.blockClass = blockClass;
        block.fieldBegin = static_cast<u32>(outLayout.typedLayoutFields.size());
        block.fieldCount = static_cast<u32>(bindStruct->fields.size());
        block.byteSize = 0u;

        if(!block.blockName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' has invalid block '{}' for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance->name)
                , StringConvert(contextName.c_str())
            );
            return false;
        }

        for(const MaterialBindField& bindField : bindStruct->fields){
            MaterialLayoutFieldType::Enum fieldType = MaterialLayoutFieldType::None;
            if(!ParseMaterialLayoutFieldType(AStringView(bindField.type), fieldType)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' has "
                    "unsupported type '{}' for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(bindField.type)
                    , StringConvert(contextName.c_str())
                );
                return false;
            }

            const u32 fieldByteSize = MaterialLayoutFieldByteSize(fieldType);
            u32 fieldOffset = 0u;
            if(
                fieldByteSize == 0u
                || !AlignMaterialLayoutFieldOffset(block.byteSize, fieldType, fieldOffset)
                || fieldOffset > Limit<u32>::s_Max - fieldByteSize
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' exceeds "
                    "u32 byte size for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(contextName.c_str())
                );
                return false;
            }

            MaterialTypedLayoutField field;
            field.fieldName = Name(AStringView(bindField.name));
            field.fieldType = fieldType;
            field.offset = fieldOffset;
            if(!field.fieldName){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' has invalid field '{}.{}' for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextName.c_str())
                );
                return false;
            }
            if(!BuildMaterialTypedLayoutDefaultValue(contextName, *instance, bindField, fieldType, field.defaultValue))
                return false;
            if(
                static_cast<usize>(field.offset) > Limit<usize>::s_Max - blockByteBegin
                || !PadMaterialTypedLayoutBytesTo(outLayout.typedBlockBytes, blockByteBegin + field.offset)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' could "
                    "not append alignment padding for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextName.c_str())
                );
                return false;
            }
            if(!AppendMaterialTypedLayoutFieldBytes(outLayout.typedBlockBytes, fieldType, field.defaultValue)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' could "
                    "not append packed default bytes for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextName.c_str())
                );
                return false;
            }

            outLayout.typedLayoutFields.push_back(field);
            block.byteSize = fieldOffset + fieldByteSize;
        }

        u32 alignedBlockByteSize = 0u;
        if(!AlignMaterialLayoutBlockByteSize(block.byteSize, alignedBlockByteSize)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' exceeds "
                "u32 byte size for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance->name)
                , StringConvert(contextName.c_str())
            );
            return false;
        }
        block.byteSize = alignedBlockByteSize;
        if(
            static_cast<usize>(block.byteSize) > Limit<usize>::s_Max - blockByteBegin
            || !PadMaterialTypedLayoutBytesTo(outLayout.typedBlockBytes, blockByteBegin + block.byteSize)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' could "
                "not append alignment padding for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance->name)
                , StringConvert(contextName.c_str())
            );
            return false;
        }

        outLayout.typedLayoutBlocks.push_back(block);
    }

    outLayout.layoutHash = MaterialBinaryPayload::ComputeMaterialTypedLayoutHash(
        outLayout.typedLayoutBlocks,
        outLayout.typedLayoutFields
    );
    if(outLayout.layoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' produced an empty layout for '{}'")
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(contextName.c_str())
        );
        return false;
    }

    usize expectedBlockByteSize = 0u;
    if(!MaterialBinaryPayload::ComputeMaterialTypedBlockByteSize(outLayout.typedLayoutBlocks, expectedBlockByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' produced invalid packed block bytes for '{}'")
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(contextName.c_str())
        );
        return false;
    }
    if(expectedBlockByteSize != outLayout.typedBlockBytes.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' produced invalid packed block bytes for '{}'")
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(contextName.c_str())
        );
        return false;
    }

    if(!BuildMaterialBindTypedLayoutBlockLookup(AStringView(contextName.c_str()), outLayout))
        return false;
    if(!BuildMaterialBindTypedLayoutParameterLookup(AStringView(contextName.c_str()), outLayout))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool FindOrBuildMaterialBindTypedLayoutImpl(
    const Name& materialInterface,
    const MaterialBindEntry& bindEntry,
    MaterialBindTypedLayoutCache& inOutCache,
    const MaterialBindTypedLayout*& outLayout,
    ScratchArena& scratchArena
){
    outLayout = nullptr;

    const auto cacheIt = inOutCache.lookup.find(materialInterface);
    if(cacheIt != inOutCache.lookup.end()){
        const usize cacheIndex = cacheIt.value();
        if(cacheIndex >= inOutCache.entries.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: cache index is out of range for interface '{}'")
                , StringConvert(materialInterface.c_str())
            );
            return false;
        }

        outLayout = &inOutCache.entries[cacheIndex];
        return true;
    }

    const usize cacheIndex = inOutCache.entries.size();
    inOutCache.entries.emplace_back(inOutCache.entries.get_allocator().arena());
    MaterialBindTypedLayout& layout = inOutCache.entries.back();
    if(!BuildMaterialBindTypedLayoutImpl(bindEntry, materialInterface, layout, scratchArena)){
        inOutCache.entries.pop_back();
        return false;
    }

    if(!inOutCache.lookup.emplace(materialInterface, cacheIndex).second){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: duplicate cache entry for interface '{}'")
            , StringConvert(materialInterface.c_str())
        );
        return false;
    }

    outLayout = &layout;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

