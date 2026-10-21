// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "bind_private.h"

#include <global/array.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialTypedValueData{
    UInt4 meta = {};
    UInt4 data = {};
};
static_assert(sizeof(MaterialTypedValueData) == sizeof(u32) * NWB_MATERIAL_TYPED_VALUE_RECORD_WORD_COUNT, "MaterialTypedValueData layout must stay stable for typed value parsing");
static_assert(alignof(MaterialTypedValueData) >= alignof(UInt4), "MaterialTypedValueData must stay SIMD-aligned");
static_assert(IsTriviallyCopyable_V<MaterialTypedValueData>, "MaterialTypedValueData must stay cheap to copy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Parses typed material parameter text such as `float3(1, 0, 0)` into packed component words plus key-hash and type metadata.
class MaterialTypedValueParser final : NoCopy{
private:
    struct MaterialParameterCall{
        AStringView type;
        AStringView arguments;
    };

    struct MaterialParameterTokens{
        Array<AStringView, NWB_MATERIAL_TYPED_VALUE_COMPONENT_COUNT> values{};
        u32 count = 0u;
    };


private:
    static constexpr u64 s_MaterialParameterKeyHashLowWordMask = 0xffffffffull;
    static constexpr u32 s_MaterialParameterKeyHashHighWordBitShift = 32u;
    static constexpr AStringView s_BoolTrueTextToken = "true";
    static constexpr AStringView s_BoolTrueNumberToken = "1";
    static constexpr AStringView s_BoolFalseTextToken = "false";
    static constexpr AStringView s_BoolFalseNumberToken = "0";


private:
    [[nodiscard]] static Expected<MaterialParameterCall> SplitMaterialParameterCall(AStringView text)noexcept;
    [[nodiscard]] static Expected<AStringView> ReadMaterialParameterToken(AStringView text, usize& inOutCursor)noexcept;
    [[nodiscard]] static Expected<MaterialParameterTokens> SplitMaterialParameterTokens(AStringView text)noexcept;
    [[nodiscard]] static Expected<u32> ParseMaterialBoolToken(AStringView token)noexcept;
    [[nodiscard]] static AStringView StripMaterialNumericSuffix(AStringView token, char suffixLower, char suffixUpper)noexcept;
    [[nodiscard]] static Expected<f32> ParseMaterialParameterF32Token(AStringView token)noexcept;
    [[nodiscard]] static Expected<u32> ParseMaterialParameterSignedToken(AStringView token, i64 minValue, i64 maxValue, u32 storageMask)noexcept;
    [[nodiscard]] static Expected<u32> ParseMaterialParameterUnsignedToken(AStringView token, u64 maxValue)noexcept;
    [[nodiscard]] static Expected<u32> ParseMaterialParameterToken(AStringView token, MaterialParameterValueType::Enum type)noexcept;
    [[nodiscard]] static bool StoreMaterialTypedValueBytes(MaterialTypedValueData& outParameter, usize byteOffset, NotNull<const void*> bytes, usize byteSize)noexcept;
    template<typename ValueType>
    [[nodiscard]] static bool StoreMaterialTypedValueScalar(MaterialTypedValueData& outParameter, u32 componentIndex, u32 value)noexcept(IsArithmetic_V<ValueType>);
    [[nodiscard]] static bool StoreMaterialTypedValueComponent(MaterialTypedValueData& outParameter, MaterialParameterValueType::Enum valueType, u32 componentIndex, u32 value)noexcept;


public:
    [[nodiscard]] static Expected<MaterialTypedValueData> Parse(const ACompactString& key, const ACompactString& value)noexcept;
    [[nodiscard]] static UInt4U ToLayoutValue(const MaterialTypedValueData& parameter)noexcept;


public:
    MaterialTypedValueParser() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

