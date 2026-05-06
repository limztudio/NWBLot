// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_asset.h"
#include "material_binary_payload.h"

#include <core/alloc/scratch.h>
#include <global/hash_utils.h>
#include <global/text_utils.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialParameterTypeText(
    const AStringView typeText,
    MaterialParameterValueType::Enum& outType,
    u32& outComponentCount
){
    outType = MaterialParameterValueType::None;
    outComponentCount = 0u;

    auto tryMatch = [&](const AStringView baseName, const AStringView vectorName, const MaterialParameterValueType::Enum type) -> bool{
        if(typeText == baseName){
            outType = type;
            outComponentCount = 1u;
            return true;
        }

        const auto parseSuffix = [&](const AStringView prefix) -> bool{
            if(typeText.size() != prefix.size() + 1u)
                return false;
            for(usize i = 0; i < prefix.size(); ++i){
                if(typeText[i] != prefix[i])
                    return false;
            }

            const char suffix = typeText[prefix.size()];
            if(suffix < '1' || suffix > '4')
                return false;

            outType = type;
            outComponentCount = static_cast<u32>(suffix - '0');
            return true;
        };

        return parseSuffix(baseName) || (!vectorName.empty() && parseSuffix(vectorName));
    };

    return
        tryMatch(AStringView("float"), AStringView("vec"), MaterialParameterValueType::Float)
        || tryMatch(AStringView("int"), AStringView("ivec"), MaterialParameterValueType::Int)
        || tryMatch(AStringView("uint"), AStringView("uvec"), MaterialParameterValueType::UInt)
        || tryMatch(AStringView("bool"), AStringView("bvec"), MaterialParameterValueType::Bool)
    ;
}

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

static bool ParseMaterialParameterToken(const AStringView token, const MaterialParameterValueType::Enum type, u32& outValue){
    const char* begin = token.data();
    const char* end = begin + token.size();

    switch(type){
    case MaterialParameterValueType::Float:{
        f64 parsed = 0.0;
        if(!ParseF64FromChars(begin, end, parsed) || !IsFinite(parsed))
            return false;
        if(parsed < static_cast<f64>(Limit<f32>::s_Min) || parsed > static_cast<f64>(Limit<f32>::s_Max))
            return false;

        const f32 converted = static_cast<f32>(parsed);
        NWB_MEMCPY(&outValue, sizeof(outValue), &converted, sizeof(converted));
        return true;
    }
    case MaterialParameterValueType::Int:{
        i64 parsed = 0;
        if(!ParseI64FromChars(begin, end, parsed))
            return false;
        if(parsed < static_cast<i64>(Limit<i32>::s_Min) || parsed > static_cast<i64>(Limit<i32>::s_Max))
            return false;

        outValue = static_cast<u32>(static_cast<i32>(parsed));
        return true;
    }
    case MaterialParameterValueType::UInt:{
        u64 parsed = 0u;
        if(!ParseU64FromChars(begin, end, parsed) || parsed > static_cast<u64>(Limit<u32>::s_Max))
            return false;

        outValue = static_cast<u32>(parsed);
        return true;
    }
    case MaterialParameterValueType::Bool:
        return ParseMaterialBoolToken(token, outValue);
    default:
        return false;
    }
}

static bool BuildMaterialParameterGpuData(
    const CompactString& key,
    const CompactString& value,
    MaterialParameterGpuData& outParameter
){
    outParameter = {};
    if(!key || !value)
        return false;

    MaterialParameterValueType::Enum valueType = MaterialParameterValueType::Float;
    u32 componentCount = 0u;
    const AStringView valueText = TrimView(value.view());
    AStringView argsText = valueText;
    AStringView typeText;
    if(SplitMaterialParameterCall(valueText, typeText, argsText)){
        if(!ParseMaterialParameterTypeText(typeText, valueType, componentCount))
            return false;
    }
    else if(ParseMaterialBoolToken(valueText, outParameter.data.x)){
        valueType = MaterialParameterValueType::Bool;
        componentCount = 1u;
    }

    AStringView tokens[4];
    u32 tokenCount = 0u;
    if(!SplitMaterialParameterTokens(argsText, tokens, tokenCount))
        return false;
    if(componentCount != 0u && tokenCount != componentCount)
        return false;
    if(componentCount == 0u)
        componentCount = tokenCount;

    for(u32 i = 0; i < tokenCount; ++i){
        if(!ParseMaterialParameterToken(tokens[i], valueType, outParameter.data.raw[i]))
            return false;
    }

    const u64 keyHash = UpdateFnv64TextCanonical(FNV64_OFFSET_BASIS, key.view());
    outParameter.meta.x = static_cast<u32>(keyHash & 0xffffffffull);
    outParameter.meta.y = static_cast<u32>(keyHash >> 32u);
    outParameter.meta.z = static_cast<u32>(valueType);
    outParameter.meta.w = componentCount;
    return true;
}

static bool LooksLikeMaterialParameterGpuValue(const AStringView text){
    const AStringView trimmed = TrimView(text);
    if(trimmed.empty())
        return true;

    u32 boolValue = 0u;
    if(ParseMaterialBoolToken(trimmed, boolValue))
        return true;

    const char first = trimmed[0];
    if((first >= '0' && first <= '9') || first == '+' || first == '-' || first == '.')
        return true;

    for(usize i = 0; i < trimmed.size(); ++i){
        if(trimmed[i] == '(' || trimmed[i] == ')')
            return true;
    }

    return false;
}

static bool IsTransparentText(const AStringView text){
    return
        EqualsAsciiIgnoreCase(text, "transparent")
        || EqualsAsciiIgnoreCase(text, "translucent")
        || EqualsAsciiIgnoreCase(text, "blend")
        || EqualsAsciiIgnoreCase(text, "alpha")
        || EqualsAsciiIgnoreCase(text, "avboit")
        || EqualsAsciiIgnoreCase(text, "true")
        || EqualsAsciiIgnoreCase(text, "1")
    ;
}

static bool ParseAlphaValue(const AStringView text, f32& outAlpha){
    const char* begin = text.data();
    const char* end = begin + text.size();
    while(begin < end && IsAsciiSpace(*begin))
        ++begin;
    while(end > begin && IsAsciiSpace(*(end - 1)))
        --end;
    if(begin == end)
        return false;

    f64 parsed = 0.0;
    if(!ParseF64FromChars(begin, end, parsed) || !IsFinite(parsed))
        return false;
    if(parsed < static_cast<f64>(Limit<f32>::s_Min) || parsed > static_cast<f64>(Limit<f32>::s_Max))
        return false;

    outAlpha = static_cast<f32>(Max<f64>(0.0, Min<f64>(1.0, parsed)));
    return true;
}

static u32 MaterialAlphaParameterPriority(const CompactString& key){
    if(key.view() == AStringView("alpha"))
        return 0u;
    if(key.view() == AStringView("opacity"))
        return 1u;

    return Limit<u32>::s_Max;
}

static u32 MaterialModeParameterPriority(const CompactString& key){
    if(key.view() == AStringView("render_mode"))
        return 0u;
    if(key.view() == AStringView("alpha_mode"))
        return 1u;
    if(key.view() == AStringView("transparency"))
        return 2u;

    return Limit<u32>::s_Max;
}

static bool LessU32x4(const UInt4& lhs, const UInt4& rhs){
    for(u32 i = 0; i < 4u; ++i){
        if(lhs.raw[i] < rhs.raw[i])
            return true;
        if(lhs.raw[i] > rhs.raw[i])
            return false;
    }

    return false;
}

static bool LessMaterialParameterGpuData(const MaterialParameterGpuData& lhs, const MaterialParameterGpuData& rhs){
    if(LessU32x4(lhs.meta, rhs.meta))
        return true;
    if(LessU32x4(rhs.meta, lhs.meta))
        return false;
    return LessU32x4(lhs.data, rhs.data);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Material::setParameter(const CompactString& key, const CompactString& value){
    if(!key)
        return false;

    const u32 alphaPriority = __hidden_material_asset::MaterialAlphaParameterPriority(key);
    if(alphaPriority < m_alphaPriority){
        f32 parsedAlpha = 1.f;
        if(!__hidden_material_asset::ParseAlphaValue(value.view(), parsedAlpha))
            return false;

        m_alpha = parsedAlpha;
        m_alphaPriority = alphaPriority;
    }

    const u32 modePriority = __hidden_material_asset::MaterialModeParameterPriority(key);
    if(modePriority < m_modePriority){
        m_modeTransparent = __hidden_material_asset::IsTransparentText(value.view());
        m_modePriority = modePriority;
    }

    m_transparent = m_modeTransparent || m_alpha < 0.999f;

    MaterialParameterGpuData parameter;
    if(__hidden_material_asset::BuildMaterialParameterGpuData(key, value, parameter)){
        for(MaterialParameterGpuData& existingParameter : m_parameters){
            if(existingParameter.meta.x == parameter.meta.x && existingParameter.meta.y == parameter.meta.y){
                existingParameter = parameter;
                return true;
            }
        }

        m_parameters.push_back(parameter);
        return true;
    }

    if(modePriority != Limit<u32>::s_Max)
        return true;

    return !__hidden_material_asset::LooksLikeMaterialParameterGpuValue(value.view());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MaterialAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Material::s_AssetTypeText)
        );
        return false;
    }

    const Material& material = static_cast<const Material&>(asset);
    if(!material.virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: virtual path is empty"));
        return false;
    }
    if(material.stageShaderCount() == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: material has no shader stages"));
        return false;
    }
    if(material.parameters().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: parameter count exceeds u32 range"));
        return false;
    }
    if(!IsFinite(material.alpha())){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: material alpha is not finite"));
        return false;
    }

    usize reserveBytes =
        sizeof(u32) + // magic
        sizeof(u32)   // version
    ;
    bool canReserve = AddBinaryStringReserveBytes(reserveBytes, AStringView(material.shaderVariant()))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(reserveBytes, material.stageShaderCount(), MaterialBinaryPayload::s_ShaderEntryBytes)
        && AddBinaryReserveBytes(reserveBytes, sizeof(f32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(reserveBytes, material.parameters().size(), MaterialBinaryPayload::s_ParameterEntryBytes)
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    AppendPOD(outBinary, MaterialBinaryPayload::s_MaterialMagic);
    AppendPOD(outBinary, MaterialBinaryPayload::s_MaterialVersion);
    if(!AppendString(outBinary, AStringView(material.shaderVariant()))){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader variant is too long"));
        return false;
    }
    AppendPOD(outBinary, material.stageShaderCount());

    const Material::StageShaderArray& stageShaders = material.stageShaders();
    for(usize shaderIndex = 0; shaderIndex < stageShaders.size(); ++shaderIndex){
        const Core::Assets::AssetRef<Shader>& shaderAsset = stageShaders[shaderIndex];
        if(!shaderAsset.valid())
            continue;

        const Core::ShaderType::Enum shaderType = static_cast<Core::ShaderType::Enum>(shaderIndex);
        if(!Core::ShaderType::IsValid(shaderType)){
            NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader stage index {} is invalid"), shaderIndex);
            return false;
        }

        AppendPOD(outBinary, shaderType);
        AppendPOD(outBinary, shaderAsset.name().hash());
    }

    AppendPOD(outBinary, material.alpha());
    const u32 materialFlags = material.transparent() ? MaterialBinaryPayload::s_MaterialFlagTransparent : 0u;
    AppendPOD(outBinary, materialFlags);
    AppendPOD(outBinary, static_cast<u32>(material.parameters().size()));

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<MaterialParameterGpuData, Core::Alloc::ScratchAllocator<MaterialParameterGpuData>> sortedParams{
        Core::Alloc::ScratchAllocator<MaterialParameterGpuData>(scratchArena)
    };
    sortedParams.reserve(material.parameters().size());
    for(const MaterialParameterGpuData& parameter : material.parameters())
        sortedParams.push_back(parameter);

    Sort(sortedParams.begin(), sortedParams.end(), __hidden_material_asset::LessMaterialParameterGpuData);

    for(const MaterialParameterGpuData& parameter : sortedParams)
        AppendPOD(outBinary, parameter);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

