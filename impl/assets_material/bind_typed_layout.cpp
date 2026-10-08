// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind_private.h"

#include <global/array.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u64 s_MaterialParameterKeyHashLowWordMask = 0xffffffffull;
static constexpr u32 s_MaterialParameterKeyHashHighWordBitShift = 32u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialParameterCall{
    AStringView type;
    AStringView arguments;
};

struct MaterialParameterTokens{
    Array<AStringView, NWB_MATERIAL_TYPED_VALUE_COMPONENT_COUNT> values{};
    u32 count = 0u;
};

struct MaterialTypedFieldBytes{
    const u8* data = nullptr;
    u32 size = 0u;
};

struct MaterialTypedValueData{
    UInt4 meta = {};
    UInt4 data = {};
};
static_assert(
    sizeof(MaterialTypedValueData) == sizeof(u32) * NWB_MATERIAL_TYPED_VALUE_RECORD_WORD_COUNT,
    "MaterialTypedValueData layout must stay stable for typed value parsing"
);
static_assert(alignof(MaterialTypedValueData) >= alignof(UInt4), "MaterialTypedValueData must stay SIMD-aligned");
static_assert(IsTriviallyCopyable_V<MaterialTypedValueData>, "MaterialTypedValueData must stay cheap to copy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialParameterCall> SplitMaterialParameterCall(const AStringView text){
    const AStringView trimmed = TrimView(text);
    usize openParen = Limit<usize>::s_Max;
    for(usize i = 0u; i < trimmed.size(); ++i){
        if(trimmed[i] == '('){
            openParen = i;
            break;
        }
    }
    if(openParen == Limit<usize>::s_Max || trimmed.empty() || trimmed[trimmed.size() - 1u] != ')')
        return MakeUnexpected(Failure{});

    const AStringView type = TrimView(trimmed.substr(0u, openParen));
    const AStringView arguments = TrimView(trimmed.substr(openParen + 1u, trimmed.size() - openParen - 2u));
    if(type.empty() || arguments.empty())
        return MakeUnexpected(Failure{});
    return MaterialParameterCall{ type, arguments };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<AStringView> ReadMaterialParameterToken(const AStringView text, usize& inOutCursor){
    while(inOutCursor < text.size() && (IsAsciiSpace(text[inOutCursor]) || text[inOutCursor] == ','))
        ++inOutCursor;
    if(inOutCursor >= text.size())
        return MakeUnexpected(Failure{});

    const usize begin = inOutCursor;
    while(inOutCursor < text.size() && !IsAsciiSpace(text[inOutCursor]) && text[inOutCursor] != ',')
        ++inOutCursor;

    const AStringView token = TrimView(text.substr(begin, inOutCursor - begin));
    if(token.empty())
        return MakeUnexpected(Failure{});
    return token;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialParameterTokens> SplitMaterialParameterTokens(const AStringView text){
    MaterialParameterTokens tokens;
    usize cursor = 0u;
    while(const auto token = ReadMaterialParameterToken(text, cursor)){
        if(tokens.count >= NWB_MATERIAL_TYPED_VALUE_COMPONENT_COUNT)
            return MakeUnexpected(Failure{});
        tokens.values[tokens.count] = *token;
        ++tokens.count;
    }
    if(tokens.count == 0u)
        return MakeUnexpected(Failure{});
    return tokens;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<u32> ParseMaterialBoolToken(const AStringView token){
    if(token == AStringView("true") || token == AStringView("1"))
        return 1u;
    if(token == AStringView("false") || token == AStringView("0"))
        return 0u;
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static AStringView StripMaterialNumericSuffix(const AStringView token, const char suffixLower, const char suffixUpper){
    if(token.size() <= 1u)
        return token;

    const char suffix = token[token.size() - 1u];
    return (suffix == suffixLower || suffix == suffixUpper) ? token.substr(0u, token.size() - 1u) : token;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<f32> ParseMaterialParameterF32Token(const AStringView token){
    const auto parsed = ParseF64FromChars(token);
    if(!parsed || !IsFinite(*parsed))
        return MakeUnexpected(Failure{});
    if(*parsed < static_cast<f64>(Limit<f32>::s_Min) || *parsed > static_cast<f64>(Limit<f32>::s_Max))
        return MakeUnexpected(Failure{});
    return static_cast<f32>(*parsed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<u32> ParseMaterialParameterSignedToken(
    const AStringView token,
    const i64 minValue,
    const i64 maxValue,
    const u32 storageMask
){
    const auto parsed = ParseI64FromChars(token);
    if(!parsed)
        return MakeUnexpected(Failure{});
    if(*parsed < minValue || *parsed > maxValue)
        return MakeUnexpected(Failure{});

    return static_cast<u32>(static_cast<u64>(*parsed) & storageMask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<u32> ParseMaterialParameterUnsignedToken(
    const AStringView token,
    const u64 maxValue
){
    const auto parsed = ParseU64FromChars(token);
    if(!parsed || *parsed > maxValue)
        return MakeUnexpected(Failure{});

    return static_cast<u32>(*parsed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<u32> ParseMaterialParameterToken(const AStringView token, const MaterialParameterValueType::Enum type){
    AStringView numericToken = token;
    if(type == MaterialParameterValueType::Float)
        numericToken = StripMaterialNumericSuffix(token, 'f', 'F');
    else if(type == MaterialParameterValueType::Half){
        numericToken = StripMaterialNumericSuffix(token, 'h', 'H');
        if(numericToken == token)
            numericToken = StripMaterialNumericSuffix(token, 'f', 'F');
    }
    else if(
        type == MaterialParameterValueType::UChar
        || type == MaterialParameterValueType::UShort
        || type == MaterialParameterValueType::UInt
    )
        numericToken = StripMaterialNumericSuffix(token, 'u', 'U');

    switch(type){
    case MaterialParameterValueType::Bool:
        return ParseMaterialBoolToken(token);
    case MaterialParameterValueType::Char:
        return ParseMaterialParameterSignedToken(
            numericToken,
            static_cast<i64>(Limit<i8>::s_Min),
            static_cast<i64>(Limit<i8>::s_Max),
            NWB_MATERIAL_TYPED_BYTE_MASK
        );
    case MaterialParameterValueType::UChar:
        return ParseMaterialParameterUnsignedToken(numericToken, static_cast<u64>(Limit<u8>::s_Max));
    case MaterialParameterValueType::Short:
        return ParseMaterialParameterSignedToken(
            numericToken,
            static_cast<i64>(Limit<i16>::s_Min),
            static_cast<i64>(Limit<i16>::s_Max),
            NWB_MATERIAL_TYPED_U16_MASK
        );
    case MaterialParameterValueType::UShort:
        return ParseMaterialParameterUnsignedToken(numericToken, static_cast<u64>(Limit<u16>::s_Max));
    case MaterialParameterValueType::Int:
        return ParseMaterialParameterSignedToken(
            numericToken,
            static_cast<i64>(Limit<i32>::s_Min),
            static_cast<i64>(Limit<i32>::s_Max),
            Limit<u32>::s_Max
        );
    case MaterialParameterValueType::UInt:
        return ParseMaterialParameterUnsignedToken(numericToken, static_cast<u64>(Limit<u32>::s_Max));
    case MaterialParameterValueType::Half:{
        const auto converted = ParseMaterialParameterF32Token(numericToken);
        if(!converted)
            return MakeUnexpected(Failure{});

        return static_cast<u32>(ConvertFloatToHalf(*converted));
    }
    case MaterialParameterValueType::Float:{
        const auto converted = ParseMaterialParameterF32Token(numericToken);
        if(!converted)
            return MakeUnexpected(Failure{});
        return BitCast<u32>(*converted);
    }
    default:
        return MakeUnexpected(Failure{});
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
    if(componentIndex >= NWB_MATERIAL_TYPED_VALUE_COMPONENT_COUNT)
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


static Expected<MaterialTypedValueData> BuildMaterialTypedValueData(
    const ACompactString& key,
    const ACompactString& value
){
    MaterialTypedValueData parameter;
    if(!key || !value)
        return MakeUnexpected(Failure{});

    const auto call = SplitMaterialParameterCall(TrimView(value.view()));
    if(!call)
        return MakeUnexpected(Failure{});
    const auto type = ParseMaterialParameterTypeText(call->type);
    if(!type)
        return MakeUnexpected(Failure{});
    const auto tokens = SplitMaterialParameterTokens(call->arguments);
    if(!tokens || tokens->count != type->componentCount)
        return MakeUnexpected(Failure{});

    for(u32 i = 0u; i < tokens->count; ++i){
        const auto parsedValue = ParseMaterialParameterToken(tokens->values[i], type->valueType);
        if(!parsedValue || !StoreMaterialTypedValueComponent(parameter, type->valueType, i, *parsedValue))
            return MakeUnexpected(Failure{});
    }

    const u64 keyHash = ComputeMaterialBindParameterKeyHash(key.view());
    parameter.meta.x = static_cast<u32>(keyHash & s_MaterialParameterKeyHashLowWordMask);
    parameter.meta.y = static_cast<u32>(keyHash >> s_MaterialParameterKeyHashHighWordBitShift);
    parameter.meta.z = static_cast<u32>(type->valueType);
    parameter.meta.w = type->componentCount;
    return parameter;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialLayoutFieldType::Enum> ParseMaterialLayoutFieldType(const AStringView typeText){
    const auto resourceType = ParseMaterialBindResourceFieldTypeText(typeText);
    if(resourceType)
        return *resourceType;
    const auto type = ParseMaterialParameterTypeText(typeText);
    if(!type)
        return MakeUnexpected(Failure{});
    const auto fieldType = MaterialLayoutFieldTypeFromParameterType(type->valueType, type->componentCount);
    if(!IsValidMaterialLayoutFieldType(fieldType))
        return MakeUnexpected(Failure{});
    return fieldType;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialBlockClass::Enum> ParseMaterialBindBlockClass(const MaterialBindStruct& bindStruct){
    if(bindStruct.findAttribute(s_MaterialConstantAttribute))
        return MaterialBlockClass::MaterialConstant;
    if(bindStruct.findAttribute(s_MaterialMutableAttribute))
        return MaterialBlockClass::MaterialMutable;
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static UInt4U ToMaterialTypedLayoutDefaultValue(const MaterialTypedValueData& parameter)noexcept{
    UInt4U result = {};
    for(u32 i = 0u; i < NWB_MATERIAL_TYPED_VALUE_COMPONENT_COUNT; ++i)
        result.raw[i] = parameter.data.raw[i];
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<UInt4U> BuildMaterialTypedLayoutDefaultValue(
    const Name& materialName,
    const MaterialBindInstance& instance,
    const MaterialBindField& bindField,
    const MaterialLayoutFieldType::Enum fieldType
){

    // Keep slots zero until the renderer resolves MaterialResourceReference against its live descriptor heap.
    if(IsMaterialLayoutResourceFieldType(fieldType))
        return UInt4U{};

    const auto keyResult = BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(bindField.name));
    if(!keyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: field '{}.{}' for '{}' exceeds ACompactString capacity")
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }
    const ACompactString& key = *keyResult;

    ACompactString defaultText;
    const AStringView defaultArgument = bindField.defaultArgument();
    if(!defaultText.assign(defaultArgument)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default for '{}.{}' in '{}' exceeds ACompactString capacity")
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }

    const auto defaultParameter = BuildMaterialTypedValueData(key, defaultText);
    if(!defaultParameter){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default '{}' for '{}.{}' in '{}' is invalid")
            , StringConvert(defaultArgument)
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }

    const MaterialLayoutFieldType::Enum defaultFieldType = MaterialLayoutFieldTypeFromParameterType(
        static_cast<MaterialParameterValueType::Enum>(defaultParameter->meta.z),
        defaultParameter->meta.w
    );
    if(defaultFieldType != fieldType){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default '{}' for '{}.{}' in '{}' "
            "does not match field type '{}'")
            , StringConvert(defaultArgument)
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.resolvedText())
            , StringConvert(bindField.type)
        );
        return MakeUnexpected(Failure{});
    }

    return ToMaterialTypedLayoutDefaultValue(*defaultParameter);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialTypedFieldBytes> GetMaterialTypedLayoutFieldBytes(
    const MaterialLayoutFieldType::Enum fieldType,
    const UInt4U& value
){
    const u32 byteSize = MaterialLayoutFieldByteSize(fieldType);
    if(byteSize == 0u || byteSize > sizeof(value))
        return MakeUnexpected(Failure{});
    return MaterialTypedFieldBytes{ reinterpret_cast<const u8*>(&value), byteSize };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool AppendMaterialTypedLayoutFieldBytes(
    Material::TypedBlockByteVector& outBlockBytes,
    const MaterialLayoutFieldType::Enum fieldType,
    const UInt4U& value
){
    const auto fieldBytes = GetMaterialTypedLayoutFieldBytes(fieldType, value);
    if(!fieldBytes)
        return false;

    const usize byteCount = outBlockBytes.size();
    if(static_cast<usize>(fieldBytes->size) > Limit<usize>::s_Max - byteCount)
        return false;

    outBlockBytes.reserve(byteCount + fieldBytes->size);
    outBlockBytes.insert(outBlockBytes.end(), fieldBytes->data, fieldBytes->data + fieldBytes->size);
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
                , StringConvert(contextName.resolvedText())
            );
            return false;
        }
        fieldReserveCount += bindStruct->fields.size();

        for(const MaterialBindField& bindField : bindStruct->fields){
            usize fieldByteReserve = sizeof(UInt4U);
            const auto fieldType = ParseMaterialLayoutFieldType(AStringView(bindField.type));
            if(fieldType){
                const u32 fieldByteSize = MaterialLayoutFieldByteSize(*fieldType);
                if(fieldByteSize != 0u)
                    fieldByteReserve = fieldByteSize;
            }

            if(byteReserveCount > Limit<usize>::s_Max - fieldByteReserve){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' exceeds supported byte count for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(contextName.resolvedText())
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


static Expected<ScratchVector<const MaterialBindInstance*>> BuildSortedMaterialBindInstances(
    const MaterialBindEntry& bindEntry,
    const Name& contextName,
    ScratchArena& arena
){
    ScratchVector<const MaterialBindInstance*> instances(arena);
    instances.reserve(bindEntry.instances.size());
    for(const MaterialBindInstance& instance : bindEntry.instances)
        instances.push_back(&instance);
    Sort(instances.begin(), instances.end(), [](const MaterialBindInstance* lhs, const MaterialBindInstance* rhs){
        return lhs->name < rhs->name;
    });
    if(instances.size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' exceeds supported block count for '{}'")
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(contextName.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }

    return instances;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool BuildMaterialBindTypedLayoutBlockLookup(
    const AStringView contextLabel,
    MaterialBindTypedLayout& inOutLayout
){
    inOutLayout.blockLookup.clear();
    inOutLayout.blockLookup.reserve(inOutLayout.typedLayoutBlocks.size());

    usize blockByteBegin = 0u;
    u32 constantByteBegin = 0u;
    for(usize blockIndex = 0u; blockIndex < inOutLayout.typedLayoutBlocks.size(); ++blockIndex){
        const MaterialTypedLayoutBlock& block = inOutLayout.typedLayoutBlocks[blockIndex];
        if(blockIndex > Limit<u32>::s_Max || blockByteBegin > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: block byte offset exceeds u32 for '{}'")
                , StringConvert(contextLabel)
            );
            return false;
        }

        const MaterialBindTypedLayoutBlockLookupEntry entry{
            static_cast<u32>(blockIndex),
            static_cast<u32>(blockByteBegin),
            constantByteBegin
        };
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
        if(block.blockClass == MaterialBlockClass::MaterialConstant){
            if(block.byteSize > Limit<u32>::s_Max - constantByteBegin){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: constant block byte size overflows for '{}'"), StringConvert(contextLabel));
                return false;
            }
            constantByteBegin += block.byteSize;
        }
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
            if(
                IsMaterialLayoutResourceFieldType(field.fieldType)
                && field.offset > Limit<u32>::s_Max - blockEntry.constantByteBegin
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' resource field '{}.{}' "
                    "constant byte offset exceeds u32 for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(bindStruct->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextLabel)
                );
                return false;
            }

            const auto parameterNameResult = BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(bindField.name));
            if(!parameterNameResult){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: field '{}.{}' for '{}' exceeds ACompactString capacity")
                    , StringConvert(instance.name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextLabel)
                );
                return false;
            }
            const ACompactString& parameterName = *parameterNameResult;

            const MaterialBindTypedLayoutParameterLookupEntry entry{
                static_cast<u32>(fieldIndex),
                blockEntry.byteBegin + field.offset,
                blockEntry.constantByteBegin + field.offset,
                block.blockName
            };
            if(!inOutLayout.parameterLookup.emplace(parameterName, entry).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: duplicate parameter '{}' in '{}'")
                    , StringConvert(parameterName.view())
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
    const auto fieldBytes = GetMaterialTypedLayoutFieldBytes(fieldType, value);
    if(!fieldBytes)
        return false;
    if(byteOffset > inOutBlockBytes.size() || static_cast<usize>(fieldBytes->size) > inOutBlockBytes.size() - byteOffset)
        return false;

    NWB_MEMCPY(inOutBlockBytes.data() + byteOffset, fieldBytes->size, fieldBytes->data, fieldBytes->size);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<UInt4U> ParseMaterialTypedLayoutParameterValue(
    const Name& materialName,
    const ACompactString& parameterName,
    const ACompactString& parameterValue,
    const MaterialTypedLayoutField& field
){

    const auto parameter = BuildMaterialTypedValueData(parameterName, parameterValue);
    if(!parameter){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' for '{}' has invalid value '{}'")
            , StringConvert(parameterName.view())
            , StringConvert(materialName.resolvedText())
            , StringConvert(parameterValue.view())
        );
        return MakeUnexpected(Failure{});
    }

    const MaterialLayoutFieldType::Enum parameterFieldType = MaterialLayoutFieldTypeFromParameterType(
        static_cast<MaterialParameterValueType::Enum>(parameter->meta.z),
        parameter->meta.w
    );
    if(parameterFieldType != field.fieldType){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' for '{}' does not match interface field type")
            , StringConvert(parameterName.view())
            , StringConvert(materialName.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }

    return ToMaterialTypedLayoutDefaultValue(*parameter);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ApplyMaterialBindTypedLayoutParameterValue(
    const MaterialBindTypedLayout& layout,
    const Name& materialName,
    const ACompactString& parameterName,
    const ACompactString& parameterValue,
    Material::TypedBlockByteVector& inOutBlockBytes,
    Material::ResourceReferenceVector& outResourceReferences
){
    const auto parameterIt = layout.parameterLookup.find(parameterName);
    if(parameterIt == layout.parameterLookup.end()){
        AStringView interfacePath = "<unknown>";
        if(layout.bindEntry)
            interfacePath = AStringView(layout.bindEntry->virtualPath);

        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: typed parameter '{}' is not declared "
            "by interface '{}' for '{}'")
            , StringConvert(parameterName.view())
            , StringConvert(interfacePath)
            , StringConvert(materialName.resolvedText())
        );
        return false;
    }
    const MaterialBindTypedLayoutParameterLookupEntry& parameterEntry = parameterIt.value();
    if(parameterEntry.fieldIndex >= layout.typedLayoutFields.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' field index is out of range for '{}'")
            , StringConvert(parameterName.view())
            , StringConvert(materialName.resolvedText())
        );
        return false;
    }

    const MaterialTypedLayoutField& field = layout.typedLayoutFields[parameterEntry.fieldIndex];
    if(IsMaterialLayoutResourceFieldType(field.fieldType)){
        const MaterialResourceKind::Enum resourceKind = MaterialLayoutFieldResourceKind(field.fieldType);
        const AStringView resourcePath = TrimView(AStringView(parameterValue));
        const Name resourceName(resourcePath);
        if(
            !parameterEntry.blockName
            || !field.fieldName
            || !resourceName
            || !IsSupportedMaterialResourceReference(resourceKind, resourcePath)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: resource parameter '{}' for '{}' must be an engine or project asset path")
                , StringConvert(parameterName.view())
                , StringConvert(materialName.resolvedText())
            );
            return false;
        }

        MaterialResourceReference resourceReference;
        resourceReference.blockName = parameterEntry.blockName;
        resourceReference.fieldName = field.fieldName;
        resourceReference.resourceKind = resourceKind;
        resourceReference.constantByteOffset = parameterEntry.constantByteOffset;
        if(!AssignMaterialResourceReferenceAsset(resourceReference, resourceKind, resourceName))
            return false;
        outResourceReferences.push_back(resourceReference);
        return true;
    }

    const auto typedValue = ParseMaterialTypedLayoutParameterValue(materialName, parameterName, parameterValue, field);
    if(!typedValue)
        return false;

    if(!WriteMaterialTypedLayoutFieldBytes(inOutBlockBytes, parameterEntry.byteOffset, field.fieldType, *typedValue)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' write exceeds packed layout bytes for '{}'")
            , StringConvert(parameterName.view())
            , StringConvert(materialName.resolvedText())
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialBindTypedLayout> BuildMaterialBindTypedLayoutImpl(
    const MaterialBindEntry& bindEntry,
    const Name& contextName,
    MaterialCookArena& arena,
    ScratchArena& scratchArena
){
    MaterialBindTypedLayout layout(arena);
    layout.bindEntry = &bindEntry;

    const auto sortedInstances = BuildSortedMaterialBindInstances(bindEntry, contextName, scratchArena);
    if(!sortedInstances)
        return MakeUnexpected(Failure{});
    if(!ReserveMaterialBindTypedLayoutVectors(bindEntry, contextName, *sortedInstances, layout))
        return MakeUnexpected(Failure{});

    u32 constantTypedByteSize = 0u;
    for(const MaterialBindInstance* instance : *sortedInstances){
        const MaterialBindStruct* bindStruct = bindEntry.findStruct(AStringView(instance->type));
        if(!bindStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' instance '{}' references unknown "
                "struct type '{}' for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance->name)
                , StringConvert(instance->type)
                , StringConvert(contextName.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }

        const auto blockClassResult = ParseMaterialBindBlockClass(*bindStruct);
        if(!blockClassResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' struct '{}' is missing "
                "a material block class for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(bindStruct->name)
                , StringConvert(contextName.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }

        const MaterialBlockClass::Enum blockClass = *blockClassResult;
        if(layout.typedLayoutFields.size() > Limit<u32>::s_Max || bindStruct->fields.size() > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' exceeds "
                "supported field count for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance->name)
                , StringConvert(contextName.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }

        const usize blockByteBegin = layout.typedBlockBytes.size();

        MaterialTypedLayoutBlock block;
        block.blockName = Name(AStringView(instance->name));
        block.blockClass = blockClass;
        block.fieldBegin = static_cast<u32>(layout.typedLayoutFields.size());
        block.fieldCount = static_cast<u32>(bindStruct->fields.size());
        block.byteSize = 0u;

        if(!block.blockName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' has invalid block '{}' for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance->name)
                , StringConvert(contextName.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }

        for(const MaterialBindField& bindField : bindStruct->fields){
            const auto fieldTypeResult = ParseMaterialLayoutFieldType(AStringView(bindField.type));
            if(!fieldTypeResult){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' has "
                    "unsupported type '{}' for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(bindField.type)
                    , StringConvert(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }
            const MaterialLayoutFieldType::Enum fieldType = *fieldTypeResult;
            if(IsMaterialLayoutResourceFieldType(fieldType) && blockClass != MaterialBlockClass::MaterialConstant){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: resource field '{}.{}' must use material-constant storage for '{}'")
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }

            const u32 fieldByteSize = MaterialLayoutFieldByteSize(fieldType);
            const auto fieldOffset = AlignMaterialLayoutFieldOffset(block.byteSize, fieldType);
            if(
                fieldByteSize == 0u || !fieldOffset || *fieldOffset > Limit<u32>::s_Max - fieldByteSize
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' exceeds "
                    "u32 byte size for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }

            MaterialTypedLayoutField field;
            field.fieldName = Name(AStringView(bindField.name));
            field.fieldType = fieldType;
            field.offset = *fieldOffset;
            if(!field.fieldName){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' has invalid field '{}.{}' for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }
            const auto defaultValue = BuildMaterialTypedLayoutDefaultValue(contextName, *instance, bindField, fieldType);
            if(!defaultValue)
                return MakeUnexpected(Failure{});
            field.defaultValue = *defaultValue;
            if(
                static_cast<usize>(field.offset) > Limit<usize>::s_Max - blockByteBegin
                || !PadMaterialTypedLayoutBytesTo(layout.typedBlockBytes, blockByteBegin + field.offset)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' could "
                    "not append alignment padding for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }
            if(!AppendMaterialTypedLayoutFieldBytes(layout.typedBlockBytes, fieldType, field.defaultValue)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' could "
                    "not append packed default bytes for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }

            layout.typedLayoutFields.push_back(field);
            block.byteSize = *fieldOffset + fieldByteSize;
        }

        const auto alignedBlockByteSize = AlignMaterialLayoutBlockByteSize(block.byteSize);
        if(!alignedBlockByteSize){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' exceeds "
                "u32 byte size for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance->name)
                , StringConvert(contextName.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }
        block.byteSize = *alignedBlockByteSize;
        if(
            static_cast<usize>(block.byteSize) > Limit<usize>::s_Max - blockByteBegin
            || !PadMaterialTypedLayoutBytesTo(layout.typedBlockBytes, blockByteBegin + block.byteSize)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' could "
                "not append alignment padding for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance->name)
                , StringConvert(contextName.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }

        layout.typedLayoutBlocks.push_back(block);
        if(blockClass == MaterialBlockClass::MaterialConstant){
            if(block.byteSize > Limit<u32>::s_Max - constantTypedByteSize){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' constant storage exceeds u32 byte size for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }
            constantTypedByteSize += block.byteSize;
        }
    }

    layout.layoutHash = MaterialBinaryPayload::ComputeMaterialTypedLayoutHash(
        layout.typedLayoutBlocks,
        layout.typedLayoutFields
    );
    if(layout.layoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' produced an empty layout for '{}'")
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(contextName.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }

    const auto expectedBlockByteSize = MaterialBinaryPayload::ComputeMaterialTypedBlockByteSize(layout.typedLayoutBlocks);
    if(!expectedBlockByteSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' produced invalid packed block bytes for '{}'")
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(contextName.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }
    if(*expectedBlockByteSize != layout.typedBlockBytes.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' produced invalid packed block bytes for '{}'")
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(contextName.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }

    if(!BuildMaterialBindTypedLayoutBlockLookup(contextName.resolvedText(), layout))
        return MakeUnexpected(Failure{});
    if(!BuildMaterialBindTypedLayoutParameterLookup(contextName.resolvedText(), layout))
        return MakeUnexpected(Failure{});

    return layout;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<const MaterialBindTypedLayout*> FindOrBuildMaterialBindTypedLayoutImpl(
    const Name& materialInterface,
    const MaterialBindEntry& bindEntry,
    MaterialBindTypedLayoutCache& inOutCache,
    ScratchArena& scratchArena
){
    const auto cacheIt = inOutCache.lookup.find(materialInterface);
    if(cacheIt != inOutCache.lookup.end()){
        const usize cacheIndex = cacheIt.value();
        if(cacheIndex >= inOutCache.entries.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: cache index is out of range for interface '{}'")
                , StringConvert(materialInterface.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }

        return &inOutCache.entries[cacheIndex];
    }

    const usize cacheIndex = inOutCache.entries.size();
    auto layout = BuildMaterialBindTypedLayoutImpl(bindEntry, materialInterface, inOutCache.entries.get_allocator().arena(), scratchArena);
    if(!layout)
        return MakeUnexpected(Failure{});

    inOutCache.entries.reserve(cacheIndex + 1u);
    if(!inOutCache.lookup.emplace(materialInterface, cacheIndex).second){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: duplicate cache entry for interface '{}'")
            , StringConvert(materialInterface.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }

    inOutCache.entries.push_back(Move(*layout));
    return &inOutCache.entries.back();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

