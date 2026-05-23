// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_bind.h"
#include "material_binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_paths.h>
#include <core/metascript/parser.h>

#include <core/common/log.h>
#include <global/hash_utils.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Metascript = Core::Metascript;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_bind{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookString = ShaderCook::CookString;
using ScratchArena = Core::Alloc::ScratchArena<>;
template<typename T>
using ScratchVector = Vector<T, ScratchArena>;

static constexpr AStringView s_AssetTypeMaterialBind = "material_bind";
static constexpr AStringView s_AssetVariableMaterialBind = "asset";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialBindDocument(const Path& bindFilePath, ShaderCook::CookArena& arena, Metascript::Document& outDoc){
    CookString bindText{arena};
    if(!ReadTextFile(bindFilePath, bindText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to read Bind '{}'"), PathToString<tchar>(bindFilePath));
        return false;
    }
    StripUtf8Bom(bindText);

    if(!outDoc.parseWithImplicitAsset(AStringView(bindText), s_AssetTypeMaterialBind, s_AssetVariableMaterialBind)){
        for(const Metascript::ParseError& err : outDoc.errors()){
            NWB_LOGGER_ERROR(NWB_TEXT("Bind '{}' parse error at {}:{}: {}")
                , PathToString<tchar>(bindFilePath)
                , err.line
                , err.column
                , StringConvert(AStringView(err.message.data(), err.message.size()))
            );
        }
        return false;
    }
    return true;
}

static const Metascript::Value* FindAssetMapValue(const Path& bindFilePath, const Metascript::Document& doc){
    const Metascript::MStringView assetVariable = doc.assetVariable();
    const Metascript::Value* asset = doc.findVariable(assetVariable);
    if(!asset){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset variable '{}' has no assignments")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(AStringView(assetVariable.data(), assetVariable.size()))
        );
        return nullptr;
    }

    if(!asset->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset is not a map"), PathToString<tchar>(bindFilePath));
        return nullptr;
    }

    return asset;
}

static bool ValidatePairedSourceExtension(const Path& bindFilePath, const CookString& sourcePath){
    Core::Alloc::ScratchArena<> scratchArena;
    AString<Core::Alloc::ScratchArena<>> extension = PathToString(scratchArena, Path(sourcePath).extension());
    CanonicalizeTextInPlace(extension);
    if(AStringView(extension) == MaterialBindNames::SourceExtensionText())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': paired source '{}' must use '{}' extension")
        , PathToString<tchar>(bindFilePath)
        , StringConvert(sourcePath)
        , StringConvert(MaterialBindNames::SourceExtensionText())
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// material bind parsing helpers


static bool IsMaterialBindIdentifier(const AStringView text){
    if(text.empty())
        return false;

    const auto isAlpha = [](const char ch){
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    };
    const auto isDigit = [](const char ch){
        return ch >= '0' && ch <= '9';
    };
    const auto isIdentifierChar = [&](const char ch){
        return isAlpha(ch) || isDigit(ch) || ch == '_';
    };

    if(!isAlpha(text[0]) && text[0] != '_')
        return false;

    for(usize i = 1u; i < text.size(); ++i){
        if(!isIdentifierChar(text[i]))
            return false;
    }
    return true;
}

static bool ParseMaterialParameterTypeText(
    const AStringView typeText,
    MaterialParameterValueType::Enum& outType,
    u32& outComponentCount
){
    outType = MaterialParameterValueType::None;
    outComponentCount = 0u;

    const auto tryMatch = [&](
        const AStringView baseName,
        const MaterialParameterValueType::Enum type
    ) -> bool{
        if(typeText == baseName){
            outType = type;
            outComponentCount = 1u;
            return true;
        }

        const auto parseSuffix = [&](const AStringView prefix) -> bool{
            if(typeText.size() != prefix.size() + 1u)
                return false;
            if(typeText.substr(0u, prefix.size()) != prefix)
                return false;

            const char suffix = typeText[prefix.size()];
            if(suffix < '2' || suffix > '4')
                return false;

            outType = type;
            outComponentCount = static_cast<u32>(suffix - '0');
            return true;
        };

        return parseSuffix(baseName);
    };

    return
        tryMatch(AStringView("float"), MaterialParameterValueType::Float)
        || tryMatch(AStringView("int"), MaterialParameterValueType::Int)
        || tryMatch(AStringView("uint"), MaterialParameterValueType::UInt)
        || tryMatch(AStringView("bool"), MaterialParameterValueType::Bool)
    ;
}

static bool IsMaterialBindBlockClassAttribute(const AStringView attributeName){
    return attributeName == "material_constant" || attributeName == "material_mutable";
}

static bool ParseMaterialBindStringField(
    const Path& bindFilePath,
    const Metascript::Value& map,
    const AStringView fieldName,
    const AStringView contextLabel,
    CookString& outValue
){
    outValue.clear();

    const Metascript::Value* value = map.findField(fieldName);
    if(!value || !value->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} field '{}' must be a string")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(contextLabel)
            , StringConvert(fieldName)
        );
        return false;
    }

    const Metascript::MStringView text = value->asString();
    outValue.assign(text.data(), text.size());
    return true;
}

static bool ParseMaterialBindAttributeList(
    const Path& bindFilePath,
    const Metascript::Value* attributesValue,
    const AStringView contextLabel,
    ShaderCook::CookArena& arena,
    ShaderCook::CookVector<MaterialBindAttribute>& outAttributes
){
    outAttributes.clear();
    if(!attributesValue)
        return true;

    if(!attributesValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} attributes must be a list")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(contextLabel)
        );
        return false;
    }

    const auto& attributeList = attributesValue->asList();
    outAttributes.reserve(attributeList.size());
    for(const Metascript::Value& attributeValue : attributeList){
        if(!attributeValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} attribute entries must be maps")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(contextLabel)
            );
            return false;
        }

        MaterialBindAttribute attribute(arena);
        if(!ParseMaterialBindStringField(bindFilePath, attributeValue, "name", contextLabel, attribute.name))
            return false;
        if(!IsMaterialBindIdentifier(attribute.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid attribute name '{}' in {}")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(attribute.name)
                , StringConvert(contextLabel)
            );
            return false;
        }

        const Metascript::Value* argumentsValue = attributeValue.findField("arguments");
        if(argumentsValue){
            if(!argumentsValue->isList()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': attribute '{}' arguments must be a list")
                    , PathToString<tchar>(bindFilePath)
                    , StringConvert(attribute.name)
                );
                return false;
            }

            const auto& argumentList = argumentsValue->asList();
            attribute.arguments.reserve(argumentList.size());
            for(const Metascript::Value& argumentValue : argumentList){
                if(!argumentValue.isString()){
                    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': attribute '{}' arguments must be strings")
                        , PathToString<tchar>(bindFilePath)
                        , StringConvert(attribute.name)
                    );
                    return false;
                }

                const Metascript::MStringView argumentText = argumentValue.asString();
                attribute.arguments.emplace_back(argumentText.data(), argumentText.size(), arena);
            }
        }

        outAttributes.push_back(Move(attribute));
    }

    return true;
}

static bool ValidateMaterialBindStructAttributes(
    const Path& bindFilePath,
    const MaterialBindStruct& bindStruct
){
    bool foundBlockClass = false;

    for(const MaterialBindAttribute& attribute : bindStruct.attributes){
        if(!IsMaterialBindBlockClassAttribute(attribute.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' has unsupported attribute '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(attribute.name)
            );
            return false;
        }
        if(!attribute.arguments.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' block class attribute '{}' must not have arguments")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(attribute.name)
            );
            return false;
        }
        if(foundBlockClass){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' declares more than one block class attribute")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
            );
            return false;
        }

        foundBlockClass = true;
    }

    if(foundBlockClass)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must declare a material block class attribute")
        , PathToString<tchar>(bindFilePath)
        , StringConvert(bindStruct.name)
    );
    return false;
}

static bool ValidateMaterialBindFieldAttributes(const Path& bindFilePath, const MaterialBindStruct& bindStruct, const MaterialBindField& field){
    bool foundDefault = false;

    for(const MaterialBindAttribute& attribute : field.attributes){
        if(attribute.name != "default"){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' has unsupported attribute '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
                , StringConvert(attribute.name)
            );
            return false;
        }
        if(foundDefault){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' declares default more than once")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }
        if(attribute.arguments.size() != 1u || attribute.arguments[0].empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' default attribute requires one non-empty string argument")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }

        foundDefault = true;
    }

    if(foundDefault)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' must declare a default attribute")
        , PathToString<tchar>(bindFilePath)
        , StringConvert(bindStruct.name)
        , StringConvert(field.name)
    );
    return false;
}

static bool ParseMaterialBindField(
    const Path& bindFilePath,
    const Metascript::Value& fieldValue,
    const MaterialBindStruct& bindStruct,
    ShaderCook::CookArena& arena,
    MaterialBindField& outField
){
    if(!fieldValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' field entries must be maps")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
        );
        return false;
    }

    if(!ParseMaterialBindStringField(bindFilePath, fieldValue, "type", bindStruct.name, outField.type))
        return false;
    if(!ParseMaterialBindStringField(bindFilePath, fieldValue, "name", bindStruct.name, outField.name))
        return false;
    MaterialParameterValueType::Enum fieldType = MaterialParameterValueType::None;
    u32 fieldComponentCount = 0u;
    if(!IsMaterialBindIdentifier(outField.type)
        || !ParseMaterialParameterTypeText(AStringView(outField.type), fieldType, fieldComponentCount)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' has unsupported type '{}'")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
            , StringConvert(outField.name)
            , StringConvert(outField.type)
        );
        return false;
    }
    if(!IsMaterialBindIdentifier(outField.name)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' has invalid name")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
            , StringConvert(outField.name)
        );
        return false;
    }
    if(!ParseMaterialBindAttributeList(bindFilePath, fieldValue.findField("attributes"), outField.name, arena, outField.attributes))
        return false;
    if(!ValidateMaterialBindFieldAttributes(bindFilePath, bindStruct, outField))
        return false;

    return true;
}

static bool ParseMaterialBindStruct(
    const Path& bindFilePath,
    const Metascript::MStringView structName,
    const Metascript::Value& structValue,
    ShaderCook::CookArena& arena,
    MaterialBindStruct& outStruct
){
    if(!structValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must be a map")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(AStringView(structName.data(), structName.size()))
        );
        return false;
    }

    outStruct.name.assign(structName.data(), structName.size());
    if(!IsMaterialBindIdentifier(outStruct.name)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid struct name '{}'")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(outStruct.name)
        );
        return false;
    }
    if(!ParseMaterialBindAttributeList(bindFilePath, structValue.findField("attributes"), outStruct.name, arena, outStruct.attributes))
        return false;
    if(!ValidateMaterialBindStructAttributes(bindFilePath, outStruct))
        return false;

    const Metascript::Value* fieldsValue = structValue.findField("fields");
    if(!fieldsValue || !fieldsValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' fields must be a list")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(outStruct.name)
        );
        return false;
    }
    if(fieldsValue->asList().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must declare at least one field")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(outStruct.name)
        );
        return false;
    }

    outStruct.fields.reserve(fieldsValue->asList().size());
    for(const Metascript::Value& fieldValue : fieldsValue->asList()){
        MaterialBindField field(arena);
        if(!ParseMaterialBindField(bindFilePath, fieldValue, outStruct, arena, field))
            return false;

        if(outStruct.findField(AStringView(field.name))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': duplicate field '{}.{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(outStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }

        outStruct.fields.push_back(Move(field));
    }

    return true;
}

static bool ParseMaterialBindStructs(const Path& bindFilePath, const Metascript::Value& asset, ShaderCook::CookArena& arena, ShaderCook::CookVector<MaterialBindStruct>& outStructs){
    outStructs.clear();

    const Metascript::Value* structsValue = asset.findField("structs");
    if(!structsValue || !structsValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.structs must be a map"), PathToString<tchar>(bindFilePath));
        return false;
    }

    const auto& structsMap = structsValue->asMap();
    if(structsMap.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.structs must not be empty"), PathToString<tchar>(bindFilePath));
        return false;
    }

    outStructs.reserve(structsMap.size());
    for(const auto& [structName, structValue] : structsMap){
        MaterialBindStruct bindStruct(arena);
        if(!ParseMaterialBindStruct(bindFilePath, Metascript::MStringView(structName.data(), structName.size()), structValue, arena, bindStruct))
            return false;
        outStructs.push_back(Move(bindStruct));
    }

    Sort(outStructs.begin(), outStructs.end(), [](const MaterialBindStruct& lhs, const MaterialBindStruct& rhs){
        return lhs.name < rhs.name;
    });
    return true;
}

static bool ParseMaterialBindInstances(const Path& bindFilePath, const Metascript::Value& asset, ShaderCook::CookArena& arena, MaterialBindEntry& outEntry){
    outEntry.instances.clear();

    const Metascript::Value* instancesValue = asset.findField("instances");
    if(!instancesValue || !instancesValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.instances must be a list"), PathToString<tchar>(bindFilePath));
        return false;
    }
    if(instancesValue->asList().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.instances must not be empty"), PathToString<tchar>(bindFilePath));
        return false;
    }

    outEntry.instances.reserve(instancesValue->asList().size());
    for(const Metascript::Value& instanceValue : instancesValue->asList()){
        if(!instanceValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.instances entries must be maps"), PathToString<tchar>(bindFilePath));
            return false;
        }

        MaterialBindInstance instance(arena);
        if(!ParseMaterialBindStringField(bindFilePath, instanceValue, "type", "instance", instance.type))
            return false;
        if(!ParseMaterialBindStringField(bindFilePath, instanceValue, "name", "instance", instance.name))
            return false;
        if(!IsMaterialBindIdentifier(instance.type) || !outEntry.findStruct(AStringView(instance.type))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': instance '{}' references unknown struct type '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(instance.name)
                , StringConvert(instance.type)
            );
            return false;
        }
        if(!IsMaterialBindIdentifier(instance.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid instance name '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(instance.name)
            );
            return false;
        }
        if(outEntry.findInstance(AStringView(instance.name))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': duplicate instance name '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(instance.name)
            );
            return false;
        }

        outEntry.instances.push_back(Move(instance));
    }

    return true;
}

static bool ParseMaterialBindSource(const Path& bindFilePath, const Metascript::Document& doc, ShaderCook::CookArena& arena, MaterialBindEntry& outEntry){
    outEntry.reset();

    outEntry.source = PathToString(arena, bindFilePath);
    if(!ValidatePairedSourceExtension(bindFilePath, outEntry.source))
        return false;

    const Metascript::Value* assetValue = FindAssetMapValue(bindFilePath, doc);
    if(!assetValue)
        return false;
    if(!Core::Assets::ValidateMetadataAssetFields(bindFilePath, *assetValue, "Material bind", { "structs", "instances" }))
        return false;

    if(!ParseMaterialBindStructs(bindFilePath, *assetValue, arena, outEntry.structs))
        return false;
    if(!ParseMaterialBindInstances(bindFilePath, *assetValue, arena, outEntry))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// material bind typed layout helpers


struct MaterialParameterGpuData{
    UInt4 meta = {};
    UInt4 data = {};
};
static_assert(
    sizeof(MaterialParameterGpuData) == sizeof(u32) * 8u,
    "MaterialParameterGpuData layout must stay stable for typed value parsing"
);
static_assert(alignof(MaterialParameterGpuData) >= alignof(UInt4), "MaterialParameterGpuData must stay SIMD-aligned");
static_assert(IsTriviallyCopyable_V<MaterialParameterGpuData>, "MaterialParameterGpuData must stay cheap to copy");

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

static AStringView StripMaterialNumericSuffix(const AStringView token, const char suffixLower, const char suffixUpper){
    if(token.size() <= 1u)
        return token;

    const char suffix = token[token.size() - 1u];
    return (suffix == suffixLower || suffix == suffixUpper) ? token.substr(0u, token.size() - 1u) : token;
}

static bool ParseMaterialParameterToken(const AStringView token, const MaterialParameterValueType::Enum type, u32& outValue){
    AStringView numericToken = token;
    if(type == MaterialParameterValueType::Float)
        numericToken = StripMaterialNumericSuffix(token, 'f', 'F');
    else if(type == MaterialParameterValueType::UInt)
        numericToken = StripMaterialNumericSuffix(token, 'u', 'U');

    const char* begin = numericToken.data();
    const char* end = begin + numericToken.size();

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
        if(!ParseMaterialParameterToken(tokens[i], valueType, outParameter.data.raw[i]))
            return false;
    }

    const u64 keyHash = ComputeMaterialBindParameterKeyHash(key.view());
    outParameter.meta.x = static_cast<u32>(keyHash & 0xffffffffull);
    outParameter.meta.y = static_cast<u32>(keyHash >> 32u);
    outParameter.meta.z = static_cast<u32>(valueType);
    outParameter.meta.w = componentCount;
    return true;
}

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

static bool ParseMaterialBindBlockClass(
    const MaterialBindStruct& bindStruct,
    MaterialBlockClass::Enum& outBlockClass
){
    outBlockClass = MaterialBlockClass::None;

    const MaterialBindAttribute* constantAttribute = bindStruct.findAttribute("material_constant");
    if(constantAttribute){
        outBlockClass = MaterialBlockClass::MaterialConstant;
        return true;
    }

    const MaterialBindAttribute* mutableAttribute = bindStruct.findAttribute("material_mutable");
    if(mutableAttribute){
        outBlockClass = MaterialBlockClass::MaterialMutable;
        return true;
    }

    return false;
}

static UInt4U ToMaterialTypedLayoutDefaultValue(const MaterialParameterGpuData& parameter){
    UInt4U result = {};
    for(u32 i = 0u; i < 4u; ++i)
        result.raw[i] = parameter.data.raw[i];
    return result;
}

static bool BuildMaterialTypedLayoutDefaultValue(
    const Name& materialName,
    const MaterialBindInstance& instance,
    const MaterialBindField& bindField,
    const MaterialLayoutFieldType::Enum fieldType,
    UInt4U& outDefaultValue
){
    outDefaultValue = {};

    CompactString key;
    if(!BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(bindField.name), key)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: field '{}.{}' for '{}' exceeds CompactString capacity")
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.c_str())
        );
        return false;
    }

    CompactString defaultText;
    const AStringView defaultArgument = bindField.defaultArgument();
    if(!defaultText.assign(defaultArgument)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default for '{}.{}' in '{}' exceeds CompactString capacity")
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.c_str())
        );
        return false;
    }

    MaterialParameterGpuData defaultParameter;
    if(!BuildMaterialParameterGpuData(key, defaultText, defaultParameter)){
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

static bool AppendMaterialTypedLayoutFieldBytes(
    Material::TypedBlockByteVector& outBlockBytes,
    const MaterialLayoutFieldType::Enum fieldType,
    const UInt4U& value
){
    const u32 fieldByteSize = MaterialLayoutFieldByteSize(fieldType);
    if(fieldByteSize == 0u || fieldByteSize > sizeof(value))
        return false;

    const usize byteCount = outBlockBytes.size();
    if(static_cast<usize>(fieldByteSize) > Limit<usize>::s_Max - byteCount)
        return false;

    outBlockBytes.reserve(byteCount + fieldByteSize);
    const u8* bytes = reinterpret_cast<const u8*>(&value);
    outBlockBytes.insert(outBlockBytes.end(), bytes, bytes + fieldByteSize);
    return true;
}

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

            CompactString parameterName;
            if(!BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(bindField.name), parameterName)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: field '{}.{}' for '{}' exceeds CompactString capacity")
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

static bool WriteMaterialTypedLayoutFieldBytes(
    Material::TypedBlockByteVector& inOutBlockBytes,
    const usize byteOffset,
    const MaterialLayoutFieldType::Enum fieldType,
    const UInt4U& value
){
    const u32 fieldByteSize = MaterialLayoutFieldByteSize(fieldType);
    if(fieldByteSize == 0u || fieldByteSize > sizeof(value))
        return false;
    if(byteOffset > inOutBlockBytes.size() || static_cast<usize>(fieldByteSize) > inOutBlockBytes.size() - byteOffset)
        return false;

    NWB_MEMCPY(inOutBlockBytes.data() + byteOffset, fieldByteSize, &value, fieldByteSize);
    return true;
}

static bool ParseMaterialTypedLayoutParameterValue(
    const Name& materialName,
    const CompactString& parameterName,
    const CompactString& parameterValue,
    const MaterialTypedLayoutField& field,
    UInt4U& outValue
){
    outValue = {};

    MaterialParameterGpuData parameter;
    if(!BuildMaterialParameterGpuData(parameterName, parameterValue, parameter)){
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

static bool ApplyMaterialBindTypedLayoutParameterValue(
    const MaterialBindTypedLayout& layout,
    const Name& materialName,
    const CompactString& parameterName,
    const CompactString& parameterValue,
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

static bool BuildMaterialBindTypedLayoutImpl(
    const MaterialBindEntry& bindEntry,
    const Name& contextName,
    MaterialBindTypedLayout& outLayout
){
    outLayout.reset();
    outLayout.bindEntry = &bindEntry;

    ScratchArena scratchArena;
    ScratchVector<const MaterialBindInstance*> sortedInstances{ scratchArena };
    if(!BuildSortedMaterialBindInstances(bindEntry, contextName, sortedInstances))
        return false;

    outLayout.typedLayoutBlocks.reserve(sortedInstances.size());
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

        outLayout.typedLayoutFields.reserve(outLayout.typedLayoutFields.size() + bindStruct->fields.size());
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
            if(fieldByteSize == 0u || block.byteSize > Limit<u32>::s_Max - fieldByteSize){
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
            field.offset = block.byteSize;
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
            block.byteSize += fieldByteSize;
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

static bool FindOrBuildMaterialBindTypedLayoutImpl(
    const Name& materialInterface,
    const MaterialBindEntry& bindEntry,
    MaterialBindTypedLayoutCache& inOutCache,
    const MaterialBindTypedLayout*& outLayout
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
    if(!BuildMaterialBindTypedLayoutImpl(bindEntry, materialInterface, layout))
        return false;

    if(!inOutCache.lookup.emplace(materialInterface, cacheIndex).second){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: duplicate cache entry for interface '{}'")
            , StringConvert(materialInterface.c_str())
        );
        return false;
    }

    outLayout = &layout;
    return true;
}



};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const MaterialBindStruct* MaterialBindEntry::findStruct(const AStringView typeName)const{
    for(const MaterialBindStruct& bindStruct : structs){
        if(AStringView(bindStruct.name) == typeName)
            return &bindStruct;
    }
    return nullptr;
}

const MaterialBindAttribute* MaterialBindField::findAttribute(const AStringView attributeName)const{
    for(const MaterialBindAttribute& attribute : attributes){
        if(AStringView(attribute.name) == attributeName)
            return &attribute;
    }
    return nullptr;
}

AStringView MaterialBindField::defaultArgument()const{
    const MaterialBindAttribute* attribute = findAttribute("default");
    return (attribute && attribute->arguments.size() == 1u) ? AStringView(attribute->arguments[0u]) : AStringView();
}

const MaterialBindField* MaterialBindStruct::findField(const AStringView fieldName)const{
    for(const MaterialBindField& field : fields){
        if(AStringView(field.name) == fieldName)
            return &field;
    }
    return nullptr;
}

const MaterialBindAttribute* MaterialBindStruct::findAttribute(const AStringView attributeName)const{
    for(const MaterialBindAttribute& attribute : attributes){
        if(AStringView(attribute.name) == attributeName)
            return &attribute;
    }
    return nullptr;
}

const MaterialBindInstance* MaterialBindEntry::findInstance(const AStringView instanceName)const{
    for(const MaterialBindInstance& instance : instances){
        if(AStringView(instance.name) == instanceName)
            return &instance;
    }
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MaterialBindTypedLayout::reset(){
    bindEntry = nullptr;
    layoutHash = 0u;
    typedLayoutBlocks.clear();
    typedLayoutFields.clear();
    typedBlockBytes.clear();
    blockLookup.clear();
    parameterLookup.clear();
}

void MaterialBindTypedLayoutCache::reserve(const usize count){
    entries.reserve(count);
    lookup.reserve(count);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildMaterialBindParameterKey(
    const AStringView instanceName,
    const AStringView fieldName,
    CompactString& outKey
){
    outKey.clear();
    return outKey.assign(instanceName) && outKey.pushBack('.') && outKey.append(fieldName);
}

u64 ComputeMaterialBindParameterKeyHash(const AStringView parameterKey){
    return UpdateFnv64TextCanonical(FNV64_OFFSET_BASIS, parameterKey);
}

bool ParseMaterialBindSource(const Path& bindFilePath, MaterialBindEntry& outEntry){
    outEntry.reset();

    ShaderCook::CookArena& arena = outEntry.source.get_allocator().arena();
    Metascript::Document doc(arena);
    if(!__hidden_material_bind::ParseMaterialBindDocument(bindFilePath, arena, doc))
        return false;

    return __hidden_material_bind::ParseMaterialBindSource(bindFilePath, doc, arena, outEntry);
}

bool BuildMaterialBindTypedLayout(
    const MaterialBindEntry& bindEntry,
    const Name& contextName,
    MaterialBindTypedLayout& outLayout
){
    return __hidden_material_bind::BuildMaterialBindTypedLayoutImpl(bindEntry, contextName, outLayout);
}

bool FindOrBuildMaterialBindTypedLayout(
    const Name& materialInterface,
    const MaterialBindEntry& bindEntry,
    MaterialBindTypedLayoutCache& inOutCache,
    const MaterialBindTypedLayout*& outLayout
){
    return __hidden_material_bind::FindOrBuildMaterialBindTypedLayoutImpl(
        materialInterface,
        bindEntry,
        inOutCache,
        outLayout
    );
}

void CopyMaterialBindTypedLayoutDefaults(
    const MaterialBindTypedLayout& layout,
    u64& outLayoutHash,
    Material::TypedLayoutBlockVector& outBlocks,
    Material::TypedLayoutFieldVector& outFields,
    Material::TypedBlockByteVector& outBlockBytes
){
    outLayoutHash = layout.layoutHash;
    outBlocks.assign(layout.typedLayoutBlocks.begin(), layout.typedLayoutBlocks.end());
    outFields.assign(layout.typedLayoutFields.begin(), layout.typedLayoutFields.end());
    outBlockBytes.assign(layout.typedBlockBytes.begin(), layout.typedBlockBytes.end());
}

bool ApplyMaterialBindTypedLayoutParameters(
    const MaterialBindTypedLayout& layout,
    const Name& materialName,
    const MaterialBindParameterMap& parameters,
    Material::TypedBlockByteVector& inOutBlockBytes
){
    for(const auto& [parameterName, parameterValue] : parameters){
        if(!__hidden_material_bind::ApplyMaterialBindTypedLayoutParameterValue(
            layout,
            materialName,
            parameterName,
            parameterValue,
            inOutBlockBytes
        ))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

