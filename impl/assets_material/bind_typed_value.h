// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "bind_private.h"

#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(MaterialTypedValueData) == sizeof(u32) * s_MaterialTypedValueRecordWordCount, "MaterialTypedValueData layout must stay stable for typed value parsing");
static_assert(alignof(MaterialTypedValueData) >= alignof(UInt4), "MaterialTypedValueData must stay SIMD-aligned");
static_assert(IsTriviallyCopyable_V<MaterialTypedValueData>, "MaterialTypedValueData must stay cheap to copy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Parses typed material parameter text such as `float3(1, 0, 0)` into packed component words plus key-hash and type metadata.
namespace MaterialTypedValueParser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<MaterialTypedValueData> Parse(const AStringView key, const AStringView value)noexcept{
    MaterialTypedValueData parameter;
    if(key.empty() || key.size() > ACompactString::s_MaxLength || value.empty() || value.size() > ACompactString::s_MaxLength)
        return MakeUnexpected(Failure{});

    const Expected<MaterialParameterCall> call = SplitMaterialParameterCall(TrimView(value));
    if(!call)
        return MakeUnexpected(Failure{});
    const Expected<MaterialParameterType> type = TryParseMaterialParameterTypeText(call->typeText);
    if(!type)
        return MakeUnexpected(Failure{});
    const Expected<MaterialParameterTokens> tokens = SplitMaterialParameterTokens(call->arguments);
    if(!tokens || tokens->count != type->componentCount)
        return MakeUnexpected(Failure{});

    for(u32 i = 0u; i < tokens->count; ++i){
        Expected<u32> parsedValue = MakeUnexpected(Failure{});
        if(type->valueType == MaterialParameterValueType::f16){
            const AStringView numericToken = NormalizeMaterialParameterNumericToken(tokens->values[i], type->valueType);
            const Expected<f32> converted = ParseMaterialParameterF32Token(numericToken);
            if(converted)
                parsedValue = static_cast<u32>(ConvertFloatToHalf(*converted));
        }
        else{
            parsedValue = ParseMaterialParameterNonHalfToken(tokens->values[i], type->valueType);
        }
        if(!parsedValue)
            return MakeUnexpected(Failure{});
        const Expected<MaterialTypedValueData> storedParameter = StoreMaterialTypedValueComponent(type->valueType, i, *parsedValue, parameter);
        if(!storedParameter)
            return MakeUnexpected(Failure{});
        parameter = *storedParameter;
    }

    parameter.meta = PackMaterialTypedValueMeta(key, *type);
    return parameter;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

