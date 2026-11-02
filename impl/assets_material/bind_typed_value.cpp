// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind_typed_value.h"

#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_bind_typed_value{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<f32> ParseMaterialParameterF32Token(const AStringView token)noexcept{
    const auto parsed = ParseF64FromChars(token);
    if(!parsed || !IsFinite(*parsed))
        return MakeUnexpected(Failure{});
    if(*parsed < static_cast<f64>(Limit<f32>::s_Min) || *parsed > static_cast<f64>(Limit<f32>::s_Max))
        return MakeUnexpected(Failure{});
    return static_cast<f32>(*parsed);
}

[[nodiscard]] static Expected<u32> ParseMaterialParameterSignedToken(const AStringView token, const i64 minValue, const i64 maxValue, const u32 storageMask)noexcept{
    const auto parsed = ParseI64(token);
    if(!parsed)
        return MakeUnexpected(Failure{});
    if(*parsed < minValue || *parsed > maxValue)
        return MakeUnexpected(Failure{});

    return static_cast<u32>(static_cast<u64>(*parsed) & storageMask);
}

[[nodiscard]] static Expected<u32> ParseMaterialParameterUnsignedToken(const AStringView token, const u64 maxValue)noexcept{
    const auto parsed = ParseU64(token);
    if(!parsed || *parsed > maxValue)
        return MakeUnexpected(Failure{});

    return static_cast<u32>(*parsed);
}

[[nodiscard]] static Expected<u32> ParseMaterialParameterToken(const AStringView token, const MaterialParameterValueType::Enum type)noexcept{
    AStringView numericToken = token;
    if(type == MaterialParameterValueType::f32)
        numericToken = StripMaterialNumericSuffix(token, static_cast<u8>('f'), static_cast<u8>('F'));
    else if(type == MaterialParameterValueType::f16){
        numericToken = StripMaterialNumericSuffix(token, static_cast<u8>('h'), static_cast<u8>('H'));
        if(numericToken == token)
            numericToken = StripMaterialNumericSuffix(token, static_cast<u8>('f'), static_cast<u8>('F'));
    }
    else if(type == MaterialParameterValueType::u8 || type == MaterialParameterValueType::u16 || type == MaterialParameterValueType::u32)
        numericToken = StripMaterialNumericSuffix(token, static_cast<u8>('u'), static_cast<u8>('U'));

    switch(type){
    case MaterialParameterValueType::b8:{
        u32 parsedValue = 0u;
        if(!TryParseMaterialBoolToken(token, parsedValue))
            return MakeUnexpected(Failure{});
        return parsedValue;
    }
    case MaterialParameterValueType::i8:
        return ParseMaterialParameterSignedToken(numericToken, static_cast<i64>(Limit<i8>::s_Min), static_cast<i64>(Limit<i8>::s_Max), Limit<u8>::s_Max);
    case MaterialParameterValueType::u8:
        return ParseMaterialParameterUnsignedToken(numericToken, static_cast<u64>(Limit<u8>::s_Max));
    case MaterialParameterValueType::i16:
        return ParseMaterialParameterSignedToken(numericToken, static_cast<i64>(Limit<i16>::s_Min), static_cast<i64>(Limit<i16>::s_Max), Limit<u16>::s_Max);
    case MaterialParameterValueType::u16:
        return ParseMaterialParameterUnsignedToken(numericToken, static_cast<u64>(Limit<u16>::s_Max));
    case MaterialParameterValueType::i32:
        return ParseMaterialParameterSignedToken(numericToken, static_cast<i64>(Limit<i32>::s_Min), static_cast<i64>(Limit<i32>::s_Max), Limit<u32>::s_Max);
    case MaterialParameterValueType::u32:
        return ParseMaterialParameterUnsignedToken(numericToken, static_cast<u64>(Limit<u32>::s_Max));
    case MaterialParameterValueType::f16:{
        const auto converted = ParseMaterialParameterF32Token(numericToken);
        if(!converted)
            return MakeUnexpected(Failure{});

        return static_cast<u32>(ConvertFloatToHalf(*converted));
    }
    case MaterialParameterValueType::f32:{
        const auto converted = ParseMaterialParameterF32Token(numericToken);
        if(!converted)
            return MakeUnexpected(Failure{});
        return BitCast<u32>(*converted);
    }
    default:
        return MakeUnexpected(Failure{});
    }
}

template<typename ValueType>
[[nodiscard]] static b8 StoreMaterialTypedValueScalar(const u32 componentIndex, const u32 value, MaterialTypedValueData& outParameter)noexcept(IsArithmetic_V<ValueType>){
    const usize byteOffset = static_cast<usize>(componentIndex) * sizeof(ValueType);
    if(byteOffset > sizeof(outParameter.data) || sizeof(ValueType) > sizeof(outParameter.data) - byteOffset)
        return false;

    const ValueType typedValue = static_cast<ValueType>(value);
    Array<u8, sizeof(outParameter.data)> packedBytes{};
    NWB_MEMCPY(packedBytes.data(), sizeof(packedBytes), &outParameter.data, sizeof(outParameter.data));
    NWB_MEMCPY(packedBytes.data() + byteOffset, sizeof(packedBytes) - byteOffset, &typedValue, sizeof(typedValue));
    NWB_MEMCPY(&outParameter.data, sizeof(outParameter.data), packedBytes.data(), sizeof(packedBytes));
    return true;
}

[[nodiscard]] static b8 StoreMaterialTypedValueComponent(const MaterialParameterValueType::Enum valueType, const u32 componentIndex, const u32 value, MaterialTypedValueData& outParameter)noexcept{
    if(componentIndex >= s_MaterialTypedValueComponentCount)
        return false;

    switch(valueType){
    case MaterialParameterValueType::b8:
        return StoreMaterialTypedValueScalar<u8>(componentIndex, value != 0u ? 1u : 0u, outParameter);
    case MaterialParameterValueType::i8:
    case MaterialParameterValueType::u8:
        return StoreMaterialTypedValueScalar<u8>(componentIndex, value, outParameter);
    case MaterialParameterValueType::i16:
    case MaterialParameterValueType::u16:
        return StoreMaterialTypedValueScalar<u16>(componentIndex, value, outParameter);
    case MaterialParameterValueType::f16:
        return StoreMaterialTypedValueScalar<Half>(componentIndex, value, outParameter);
    default:
        outParameter.data.raw[componentIndex] = value;
        return true;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialTypedValueData> MaterialTypedValueParser::Parse(const AStringView key, const AStringView value)noexcept{
    using namespace __hidden_bind_typed_value;
    MaterialTypedValueData parameter;
    if(key.empty() || key.size() > ACompactString::s_MaxLength || value.empty() || value.size() > ACompactString::s_MaxLength)
        return MakeUnexpected(Failure{});

    MaterialParameterCall call;
    if(!SplitMaterialParameterCall(TrimView(value), call))
        return MakeUnexpected(Failure{});
    MaterialParameterType type;
    if(!TryParseMaterialParameterTypeText(call.typeText, type))
        return MakeUnexpected(Failure{});
    MaterialParameterTokens tokens;
    if(!SplitMaterialParameterTokens(call.arguments, tokens) || tokens.count != type.componentCount)
        return MakeUnexpected(Failure{});

    for(u32 i = 0u; i < tokens.count; ++i){
        const auto parsedValue = ParseMaterialParameterToken(tokens.values[i], type.valueType);
        if(!parsedValue || !StoreMaterialTypedValueComponent(type.valueType, i, *parsedValue, parameter))
            return MakeUnexpected(Failure{});
    }

    parameter.meta = PackMaterialTypedValueMeta(key, type);
    return parameter;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

