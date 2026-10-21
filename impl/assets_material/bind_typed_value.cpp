// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind_typed_value.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialTypedValueParser::MaterialParameterCall> MaterialTypedValueParser::SplitMaterialParameterCall(const AStringView text)noexcept{
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

Expected<AStringView> MaterialTypedValueParser::ReadMaterialParameterToken(const AStringView text, usize& inOutCursor)noexcept{
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

Expected<MaterialTypedValueParser::MaterialParameterTokens> MaterialTypedValueParser::SplitMaterialParameterTokens(const AStringView text)noexcept{
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

Expected<u32> MaterialTypedValueParser::ParseMaterialBoolToken(const AStringView token)noexcept{
    if(token == s_BoolTrueTextToken || token == s_BoolTrueNumberToken)
        return 1u;
    if(token == s_BoolFalseTextToken || token == s_BoolFalseNumberToken)
        return 0u;
    return MakeUnexpected(Failure{});
}

AStringView MaterialTypedValueParser::StripMaterialNumericSuffix(const AStringView token, const char suffixLower, const char suffixUpper)noexcept{
    if(token.size() <= 1u)
        return token;

    const char suffix = token[token.size() - 1u];
    return (suffix == suffixLower || suffix == suffixUpper) ? token.substr(0u, token.size() - 1u) : token;
}

Expected<f32> MaterialTypedValueParser::ParseMaterialParameterF32Token(const AStringView token)noexcept{
    const auto parsed = ParseF64FromChars(token);
    if(!parsed || !IsFinite(*parsed))
        return MakeUnexpected(Failure{});
    if(*parsed < static_cast<f64>(Limit<f32>::s_Min) || *parsed > static_cast<f64>(Limit<f32>::s_Max))
        return MakeUnexpected(Failure{});
    return static_cast<f32>(*parsed);
}

Expected<u32> MaterialTypedValueParser::ParseMaterialParameterSignedToken(const AStringView token, const i64 minValue, const i64 maxValue, const u32 storageMask)noexcept{
    const auto parsed = ParseI64FromChars(token);
    if(!parsed)
        return MakeUnexpected(Failure{});
    if(*parsed < minValue || *parsed > maxValue)
        return MakeUnexpected(Failure{});

    return static_cast<u32>(static_cast<u64>(*parsed) & storageMask);
}

Expected<u32> MaterialTypedValueParser::ParseMaterialParameterUnsignedToken(const AStringView token, const u64 maxValue)noexcept{
    const auto parsed = ParseU64FromChars(token);
    if(!parsed || *parsed > maxValue)
        return MakeUnexpected(Failure{});

    return static_cast<u32>(*parsed);
}

Expected<u32> MaterialTypedValueParser::ParseMaterialParameterToken(const AStringView token, const MaterialParameterValueType::Enum type)noexcept{
    AStringView numericToken = token;
    if(type == MaterialParameterValueType::Float)
        numericToken = StripMaterialNumericSuffix(token, 'f', 'F');
    else if(type == MaterialParameterValueType::Half){
        numericToken = StripMaterialNumericSuffix(token, 'h', 'H');
        if(numericToken == token)
            numericToken = StripMaterialNumericSuffix(token, 'f', 'F');
    }
    else if(type == MaterialParameterValueType::UChar || type == MaterialParameterValueType::UShort || type == MaterialParameterValueType::UInt)
        numericToken = StripMaterialNumericSuffix(token, 'u', 'U');

    switch(type){
    case MaterialParameterValueType::Bool:
        return ParseMaterialBoolToken(token);
    case MaterialParameterValueType::Char:
        return ParseMaterialParameterSignedToken(numericToken, static_cast<i64>(Limit<i8>::s_Min), static_cast<i64>(Limit<i8>::s_Max), NWB_MATERIAL_TYPED_BYTE_MASK);
    case MaterialParameterValueType::UChar:
        return ParseMaterialParameterUnsignedToken(numericToken, static_cast<u64>(Limit<u8>::s_Max));
    case MaterialParameterValueType::Short:
        return ParseMaterialParameterSignedToken(numericToken, static_cast<i64>(Limit<i16>::s_Min), static_cast<i64>(Limit<i16>::s_Max), NWB_MATERIAL_TYPED_U16_MASK);
    case MaterialParameterValueType::UShort:
        return ParseMaterialParameterUnsignedToken(numericToken, static_cast<u64>(Limit<u16>::s_Max));
    case MaterialParameterValueType::Int:
        return ParseMaterialParameterSignedToken(numericToken, static_cast<i64>(Limit<i32>::s_Min), static_cast<i64>(Limit<i32>::s_Max), Limit<u32>::s_Max);
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

bool MaterialTypedValueParser::StoreMaterialTypedValueBytes(MaterialTypedValueData& outParameter, const usize byteOffset, const NotNull<const void*> bytes, const usize byteSize)noexcept{
    if(byteOffset > sizeof(outParameter.data) || byteSize > sizeof(outParameter.data) - byteOffset)
        return false;

    const NotNull<u8*> outBytes(reinterpret_cast<u8*>(&outParameter.data));
    NWB_MEMCPY(outBytes.get() + byteOffset, sizeof(outParameter.data) - byteOffset, bytes.get(), byteSize);
    return true;
}

template<typename ValueType>
bool MaterialTypedValueParser::StoreMaterialTypedValueScalar(MaterialTypedValueData& outParameter, const u32 componentIndex, const u32 value)noexcept(IsArithmetic_V<ValueType>){
    const usize byteOffset = static_cast<usize>(componentIndex) * sizeof(ValueType);
    const ValueType typedValue = static_cast<ValueType>(value);
    return StoreMaterialTypedValueBytes(outParameter, byteOffset, NotNull<const void*>(&typedValue), sizeof(typedValue));
}

bool MaterialTypedValueParser::StoreMaterialTypedValueComponent(MaterialTypedValueData& outParameter, const MaterialParameterValueType::Enum valueType, const u32 componentIndex, const u32 value)noexcept{
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

Expected<MaterialTypedValueData> MaterialTypedValueParser::Parse(const ACompactString& key, const ACompactString& value)noexcept{
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

UInt4U MaterialTypedValueParser::ToLayoutValue(const MaterialTypedValueData& parameter)noexcept{
    UInt4U result = {};
    for(u32 i = 0u; i < NWB_MATERIAL_TYPED_VALUE_COMPONENT_COUNT; ++i)
        result.raw[i] = parameter.data.raw[i];
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

