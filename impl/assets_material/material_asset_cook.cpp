// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_asset_cook.h"
#include "material_binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_paths.h>
#include <core/filesystem/filesystem.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_stage_names.h>
#include <core/metascript/parser.h>
#include <global/hash_utils.h>
#include <global/text_utils.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookString = ShaderCook::CookString;
using ScratchArena = Core::Alloc::ScratchArena<>;
using ScratchString = AString<ScratchArena>;
template<typename T>
using ScratchVector = Vector<T, ScratchArena>;
template<typename T>
using ScratchHashSet = HashSet<T, Hasher<T>, EqualTo<T>, ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Path BuildMaterialBindIncludeRoot(const Path& cacheDirectory, const AStringView configurationSafeName){
    ScratchArena scratchArena;
    ScratchString configurationName(configurationSafeName, scratchArena);
    ScratchString includeDirectoryName(MaterialBindNames::GeneratedIncludeCacheDirectoryText(), scratchArena);
    return cacheDirectory / configurationName.c_str() / includeDirectoryName.c_str();
}

static bool BuildMaterialBindIncludeVirtualPathImpl(
    ShaderCook::CookArena& arena,
    const MaterialBindEntry& entry,
    CookString& outIncludePath
){
    outIncludePath.clear();
    if(entry.virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation failed: virtual path is empty for '{}'")
            , StringConvert(entry.source)
        );
        return false;
    }

    CookString includePath(entry.virtualPath, arena);
    includePath += MaterialBindNames::SourceExtensionText();
    outIncludePath = Move(includePath);
    return true;
}

static bool PathHasDirectoryAncestor(const Path& normalizedPath, const Path& normalizedDirectory){
    if(normalizedPath.empty() || normalizedDirectory.empty())
        return false;

    for(Path parent = normalizedPath.parent_path(); !parent.empty();){
        if(parent == normalizedDirectory)
            return true;

        const Path nextParent = parent.parent_path();
        if(nextParent == parent)
            break;
        parent = nextParent;
    }

    return false;
}

static bool TryResolveMaterialBindDependencyInterface(
    const Path& normalizedMaterialBindIncludeRoot,
    const Path& dependency,
    CookString& outInterfacePath
){
    outInterfacePath.clear();
    if(normalizedMaterialBindIncludeRoot.empty())
        return true;

    ErrorCode errorCode;
    Path normalizedDependency = AbsolutePath(dependency, errorCode).lexically_normal();
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to normalize shader dependency '{}': {}")
            , PathToString<tchar>(dependency)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    if(!PathHasDirectoryAncestor(normalizedDependency, normalizedMaterialBindIncludeRoot))
        return true;

    ScratchArena scratchArena;
    ScratchString extension = PathToString(scratchArena, normalizedDependency.extension());
    CanonicalizeTextInPlace(extension);
    if(extension != MaterialBindNames::SourceExtensionText())
        return true;

    Path relativePath = normalizedDependency.lexically_relative(normalizedMaterialBindIncludeRoot);
    relativePath.replace_extension();
    if(!Core::Assets::AssetPathsDetail::BuildRelativeAssetPathText(relativePath, outInterfacePath)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to derive interface from generated include '{}'")
            , PathToString<tchar>(normalizedDependency)
        );
        return false;
    }

    return true;
}

static bool ResolveMaterialBindDependencyInterface(
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const ShaderCook::CookVector<Path>& dependencies,
    CookString& outInterfacePath,
    Name& outInterfaceName
){
    outInterfacePath.clear();
    outInterfaceName = NAME_NONE;

    Path normalizedMaterialBindIncludeRoot;
    if(!materialBindIncludeRoot.empty()){
        ErrorCode errorCode;
        normalizedMaterialBindIncludeRoot = AbsolutePath(materialBindIncludeRoot, errorCode).lexically_normal();
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to normalize generated include root '{}': {}")
                , PathToString<tchar>(materialBindIncludeRoot)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    ShaderCook::CookArena& arena = outInterfacePath.get_allocator().arena();
    CookString dependencyInterfacePath{arena};
    for(const Path& dependency : dependencies){
        if(!TryResolveMaterialBindDependencyInterface(normalizedMaterialBindIncludeRoot, dependency, dependencyInterfacePath))
            return false;
        if(dependencyInterfacePath.empty())
            continue;

        const Name dependencyInterfaceName{ AStringView(dependencyInterfacePath) };
        if(!dependencyInterfaceName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: shader '{}' includes invalid generated "
                "material bind interface '{}'")
                , StringConvert(shaderName)
                , StringConvert(dependencyInterfacePath)
            );
            return false;
        }

        if(!outInterfaceName){
            outInterfacePath = dependencyInterfacePath;
            outInterfaceName = dependencyInterfaceName;
            continue;
        }

        if(outInterfaceName != dependencyInterfaceName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: shader '{}' includes multiple generated "
                "material bind interfaces ('{}' and '{}')")
                , StringConvert(shaderName)
                , StringConvert(outInterfacePath)
                , StringConvert(dependencyInterfacePath)
            );
            return false;
        }
    }

    return true;
}


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

static bool BuildMaterialTypedLayoutFieldKey(
    const AStringView blockName,
    const AStringView fieldName,
    CompactString& outKey
){
    outKey.clear();
    return outKey.assign(blockName) && outKey.pushBack('.') && outKey.append(fieldName);
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
    if(!BuildMaterialTypedLayoutFieldKey(AStringView(instance.name), AStringView(bindField.name), key)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: typed layout field '{}.{}' for '{}' exceeds CompactString capacity")
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.c_str())
        );
        return false;
    }

    CompactString defaultText;
    const AStringView defaultArgument = bindField.defaultArgument();
    if(!defaultText.assign(defaultArgument)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: typed layout default for '{}.{}' in '{}' exceeds CompactString capacity")
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.c_str())
        );
        return false;
    }

    MaterialParameterGpuData defaultParameter;
    if(!BuildMaterialParameterGpuData(key, defaultText, defaultParameter)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: typed layout default '{}' for '{}.{}' in '{}' is invalid")
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
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: typed layout default '{}' for '{}.{}' in '{}' does not match field type '{}'")
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

struct MaterialTypedLayoutBlockLookupEntry{
    const MaterialTypedLayoutBlock* block = nullptr;
    u32 byteBegin = 0u;
};

struct MaterialTypedLayoutParameterLookupEntry{
    const MaterialTypedLayoutField* field = nullptr;
    u32 byteOffset = 0u;
};

using MaterialTypedLayoutBlockLookup = HashMap<
    Name,
    MaterialTypedLayoutBlockLookupEntry,
    Hasher<Name>,
    EqualTo<Name>,
    ScratchArena
>;
using MaterialTypedLayoutParameterLookup = HashMap<
    CompactString,
    MaterialTypedLayoutParameterLookupEntry,
    Hasher<CompactString>,
    EqualTo<CompactString>,
    ScratchArena
>;

struct MaterialTypedLayoutCacheEntry{
    const MaterialBindEntry* bindEntry = nullptr;
    u64 typedLayoutHash = 0u;
    Material::TypedLayoutBlockVector typedLayoutBlocks;
    Material::TypedLayoutFieldVector typedLayoutFields;
    Material::TypedBlockByteVector typedBlockBytes;

    explicit MaterialTypedLayoutCacheEntry(ShaderCook::CookArena& arena)
        : typedLayoutBlocks(arena)
        , typedLayoutFields(arena)
        , typedBlockBytes(arena)
    {}
};

using MaterialTypedLayoutCacheLookup = HashMap<
    Name,
    usize,
    Hasher<Name>,
    EqualTo<Name>,
    ScratchArena
>;

static bool BuildMaterialTypedLayoutBlockLookup(
    const Material::TypedLayoutBlockVector& blocks,
    const AStringView contextLabel,
    MaterialTypedLayoutBlockLookup& outLookup
){
    outLookup.clear();
    outLookup.reserve(blocks.size());

    usize blockByteBegin = 0u;
    for(const MaterialTypedLayoutBlock& block : blocks){
        if(blockByteBegin > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: typed layout block byte offset exceeds u32 for '{}'")
                , StringConvert(contextLabel)
            );
            return false;
        }

        const MaterialTypedLayoutBlockLookupEntry entry{ &block, static_cast<u32>(blockByteBegin) };
        if(!outLookup.emplace(block.blockName, entry).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: duplicate typed layout block in '{}'"), StringConvert(contextLabel));
            return false;
        }
        if(static_cast<usize>(block.byteSize) > Limit<usize>::s_Max - blockByteBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: typed layout block byte size overflows for '{}'")
                , StringConvert(contextLabel)
            );
            return false;
        }

        blockByteBegin += block.byteSize;
    }

    return true;
}

static bool BuildMaterialTypedLayoutParameterLookup(
    const MaterialBindEntry& bindEntry,
    const Material::TypedLayoutFieldVector& fields,
    const MaterialTypedLayoutBlockLookup& blockLookup,
    const AStringView contextLabel,
    MaterialTypedLayoutParameterLookup& outLookup
){
    outLookup.clear();
    outLookup.reserve(fields.size());

    for(const MaterialBindInstance& instance : bindEntry.instances){
        const MaterialBindStruct* bindStruct = bindEntry.findStruct(AStringView(instance.type));
        if(!bindStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' instance '{}' references unknown "
                "struct type '{}' for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance.name)
                , StringConvert(instance.type)
                , StringConvert(contextLabel)
            );
            return false;
        }

        const auto blockIt = blockLookup.find(Name(AStringView(instance.name)));
        if(blockIt == blockLookup.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' instance '{}' has no typed layout block for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance.name)
                , StringConvert(contextLabel)
            );
            return false;
        }

        const MaterialTypedLayoutBlockLookupEntry& blockEntry = blockIt.value();
        const MaterialTypedLayoutBlock& block = *blockEntry.block;
        if(block.fieldCount != bindStruct->fields.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' instance '{}' typed layout field count mismatch for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance.name)
                , StringConvert(contextLabel)
            );
            return false;
        }

        for(u32 fieldOffset = 0u; fieldOffset < block.fieldCount; ++fieldOffset){
            const usize fieldIndex = static_cast<usize>(block.fieldBegin) + fieldOffset;
            if(fieldIndex >= fields.size()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' instance '{}' typed layout field "
                    "range exceeds layout for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance.name)
                    , StringConvert(contextLabel)
                );
                return false;
            }

            const MaterialBindField& bindField = bindStruct->fields[fieldOffset];
            const MaterialTypedLayoutField& field = fields[fieldIndex];
            if(field.fieldName != Name(AStringView(bindField.name))){
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' field '{}.{}' typed layout metadata mismatch for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(bindStruct->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextLabel)
                );
                return false;
            }
            if(field.offset > Limit<u32>::s_Max - blockEntry.byteBegin){
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' field '{}.{}' byte offset exceeds u32 for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(bindStruct->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextLabel)
                );
                return false;
            }

            CompactString parameterName;
            if(!BuildMaterialTypedLayoutFieldKey(AStringView(instance.name), AStringView(bindField.name), parameterName)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: typed layout field '{}.{}' for '{}' exceeds CompactString capacity")
                    , StringConvert(instance.name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextLabel)
                );
                return false;
            }

            const MaterialTypedLayoutParameterLookupEntry entry{ &field, blockEntry.byteBegin + field.offset };
            if(!outLookup.emplace(parameterName, entry).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: duplicate typed layout parameter '{}' in '{}'")
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
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: typed parameter '{}' for '{}' has invalid value '{}'")
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
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: typed parameter '{}' for '{}' does not match interface field type")
            , StringConvert(parameterName.c_str())
            , StringConvert(materialName.c_str())
        );
        return false;
    }

    outValue = ToMaterialTypedLayoutDefaultValue(parameter);
    return true;
}

static bool ApplyMaterialTypedLayoutParameterValue(
    const MaterialBindEntry& bindEntry,
    const MaterialTypedLayoutParameterLookup& parameterLookup,
    const CompactString& parameterName,
    const CompactString& parameterValue,
    MaterialCookEntry& inOutMaterialEntry
){
    const auto parameterIt = parameterLookup.find(parameterName);
    if(parameterIt == parameterLookup.end()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: typed parameter '{}' is not declared by interface '{}' for '{}'")
            , StringConvert(parameterName.c_str())
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(inOutMaterialEntry.virtualPath.c_str())
        );
        return false;
    }
    const MaterialTypedLayoutParameterLookupEntry& parameterEntry = parameterIt.value();

    UInt4U typedValue = {};
    if(!ParseMaterialTypedLayoutParameterValue(
        inOutMaterialEntry.virtualPath,
        parameterName,
        parameterValue,
        *parameterEntry.field,
        typedValue
    ))
        return false;

    if(!WriteMaterialTypedLayoutFieldBytes(
        inOutMaterialEntry.typedBlockBytes,
        parameterEntry.byteOffset,
        parameterEntry.field->fieldType,
        typedValue
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: typed parameter '{}' write exceeds packed layout bytes for '{}'")
            , StringConvert(parameterName.c_str())
            , StringConvert(inOutMaterialEntry.virtualPath.c_str())
        );
        return false;
    }

    return true;
}

static bool IsMaterialPixelShaderStage(const Core::ShaderType::Enum shaderType){
    return shaderType == Core::ShaderType::PixelStage;
}

static bool IsMaterialMeshShaderStage(const Core::ShaderType::Enum shaderType){
    return shaderType == Core::ShaderType::MeshStage;
}

static bool IsSupportedRendererMaterialShaderStage(const Core::ShaderType::Enum shaderType){
    return IsMaterialPixelShaderStage(shaderType) || IsMaterialMeshShaderStage(shaderType);
}

static bool IsMaterialAssetField(const AStringView fieldName){
    return
        fieldName == "interface"
        || fieldName == "shaders"
        || fieldName == "shader_variant"
        || fieldName == "parameters"
        || fieldName == "alpha"
        || fieldName == "transparent"
    ;
}

static bool ValidateMaterialAssetFields(const Path& nwbFilePath, const Core::Metascript::Value& asset){
    for(const auto& [fieldName, fieldValue] : asset.asMap()){
        static_cast<void>(fieldValue);

        const AStringView fieldNameText(fieldName.data(), fieldName.size());
        if(IsMaterialAssetField(fieldNameText))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': unsupported asset field '{}'")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldNameText)
        );
        return false;
    }
    return true;
}

static bool ParseVariantField(
    ShaderCook& shaderCook,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    ShaderCook::CookString& outVariant
){
    auto& arena = outVariant.get_allocator().arena();
    outVariant.clear();

    const auto* variantValue = asset.findField(fieldName);
    if(!variantValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' is required")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    ShaderCook::CookString rawVariant{arena};
    if(variantValue->isList()){
        const auto& list = variantValue->asList();
        usize rawVariantSize = list.empty() ? 0u : list.size() - 1u;
        for(usize i = 0; i < list.size(); ++i){
            if(!list[i].isString()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' list elements must be strings")
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(fieldName)
                );
                return false;
            }
            rawVariantSize += list[i].asString().size();
        }

        rawVariant.reserve(rawVariantSize);
        for(usize i = 0; i < list.size(); ++i){
            if(i > 0)
                rawVariant += ';';
            const Core::Metascript::MStringView variantText = list[i].asString();
            rawVariant.append(variantText.data(), variantText.size());
        }
    }
    else if(variantValue->isString()){
        const Core::Metascript::MStringView variantText = variantValue->asString();
        rawVariant.assign(variantText.data(), variantText.size());
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must be a string or list of strings")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const AStringView rawVariantView = TrimView(rawVariant);
    if(rawVariantView.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    if(rawVariantView == Core::ShaderArchive::s_DefaultVariant){
        outVariant = Core::ShaderArchive::s_DefaultVariant;
        return true;
    }

    ShaderCook::CookString canonicalVariant{arena};
    if(!shaderCook.canonicalizeVariantSignature(rawVariantView, canonicalVariant)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' has invalid variant signature '{}'")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
            , StringConvert(rawVariantView)
        );
        return false;
    }

    outVariant = Move(canonicalVariant);
    return true;
}

static bool ParseMaterialStageShaders(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    MaterialCookEntry::StageShaderMap& outStageShaders
){
    outStageShaders.clear();

    const auto* shadersValue = asset.findField("shaders");
    if(!shadersValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shaders is required"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(!shadersValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shaders must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    outStageShaders.reserve(shadersValue->asMap().size());

    for(const auto& [stageKey, shaderValue] : shadersValue->asMap()){
        const AStringView stageKeyText(stageKey.data(), stageKey.size());
        if(!shaderValue.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader '{}' must be a string")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(stageKeyText)
            );
            return false;
        }

        const Core::ShaderType::Enum shaderType =
            Core::ShaderStageNames::ShaderTypeFromArchiveStageName(ToName(stageKeyText));
        const Name shaderName = ToName(shaderValue.asString());
        Core::Assets::AssetRef<Shader> shaderAsset;
        shaderAsset.virtualPath = shaderName;
        if(!Core::ShaderType::IsValid(shaderType) || !shaderAsset.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage entries must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }
        if(!IsSupportedRendererMaterialShaderStage(shaderType)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage '{}' is not supported by the ECS renderer material contract; only 'mesh' and 'ps' are allowed")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(stageKeyText)
            );
            return false;
        }

        if(!outStageShaders.emplace(shaderType, shaderAsset).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': duplicate shader stage '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(stageKeyText)
            );
            return false;
        }
    }

    if(outStageShaders.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shaders must not be empty"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    return true;
}

static bool ParseMaterialParameters(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    MaterialCookEntry::ParameterMap& outParameters
){
    outParameters.clear();

    const auto* parametersValue = asset.findField("parameters");
    if(!parametersValue)
        return true;
    if(!parametersValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameters must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    outParameters.reserve(parametersValue->asMap().size());

    auto appendParameter = [&](
        const AStringView paramKeyText,
        const Core::Metascript::Value& paramValue
    ) -> bool{
        if(!paramValue.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}' must be a string")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(paramKeyText)
            );
            return false;
        }

        CompactString key;
        CompactString value;
        const AStringView paramValueText(paramValue.asString().data(), paramValue.asString().size());
        if(!key.assign(paramKeyText) || !value.assign(paramValueText)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}' exceeds CompactString capacity")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(paramKeyText)
            );
            return false;
        }
        if(!key){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter names must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }

        if(!outParameters.emplace(key, value).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': duplicate parameter '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(key.c_str())
            );
            return false;
        }

        return true;
    };

    for(const auto& [paramKey, paramValue] : parametersValue->asMap()){
        const AStringView paramKeyText(paramKey.data(), paramKey.size());
        if(paramValue.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface parameter '{}' must be declared inside a block map")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(paramKeyText)
            );
            return false;
        }

        if(!paramValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}' must be a block map")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(paramKeyText)
            );
            return false;
        }
        if(paramKeyText.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter block names must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }

        for(const auto& [blockParamKey, blockParamValue] : paramValue.asMap()){
            const AStringView blockParamKeyText(blockParamKey.data(), blockParamKey.size());
            if(blockParamKeyText.empty()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter names in block '{}' must not be empty")
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(paramKeyText)
                );
                return false;
            }

            CompactString flattenedKey;
            if(!flattenedKey.assign(paramKeyText) || !flattenedKey.pushBack('.') || !flattenedKey.append(blockParamKeyText)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}.{}' exceeds CompactString capacity")
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(paramKeyText)
                    , StringConvert(blockParamKeyText)
                );
                return false;
            }

            if(!appendParameter(flattenedKey.view(), blockParamValue))
                return false;
        }
    }

    return true;
}

static bool ParseMaterialInterface(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Name& outMaterialInterface
){
    outMaterialInterface = NAME_NONE;

    const auto* interfaceValue = asset.findField("interface");
    if(!interfaceValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface is required"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(!interfaceValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface must be a string"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    const Core::Metascript::MStringView interfaceText = interfaceValue->asString();
    const AStringView interfacePath(interfaceText.data(), interfaceText.size());
    if(TrimView(interfacePath).empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface must not be empty"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    outMaterialInterface = Name(interfacePath);
    if(!outMaterialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface '{}' is invalid")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(interfacePath)
        );
        return false;
    }

    return true;
}

static bool ParseMaterialAlphaProperty(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    f32& outAlpha
){
    outAlpha = 1.f;

    const auto* alphaValue = asset.findField("alpha");
    if(!alphaValue)
        return true;

    f64 parsedAlpha = 0.0;
    if(alphaValue->isInteger())
        parsedAlpha = static_cast<f64>(alphaValue->asInteger());
    else if(alphaValue->isDouble())
        parsedAlpha = alphaValue->asDouble();
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': alpha must be a number"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    if(!IsFinite(parsedAlpha) || parsedAlpha < 0.0 || parsedAlpha > 1.0){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': alpha must be in the [0, 1] range"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    outAlpha = static_cast<f32>(parsedAlpha);
    return true;
}

static bool ParseMaterialTransparentProperty(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    bool& outTransparent
){
    outTransparent = false;

    const auto* transparentValue = asset.findField("transparent");
    if(!transparentValue)
        return true;
    if(!transparentValue->isInteger()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': transparent must be 0 or 1"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    const i64 transparentInt = transparentValue->asInteger();
    if(transparentInt != 0 && transparentInt != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': transparent must be 0 or 1"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    outTransparent = transparentInt != 0;
    return true;
}

static bool ParseMaterialRenderProperties(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    MaterialCookEntry& outEntry
){
    if(!ParseMaterialAlphaProperty(nwbFilePath, asset, outEntry.alpha))
        return false;
    if(!ParseMaterialTransparentProperty(nwbFilePath, asset, outEntry.transparent))
        return false;

    outEntry.transparent = outEntry.transparent || outEntry.alpha < 0.999f;
    return true;
}

using MaterialBindInterfaceLookup = HashMap<
    Name,
    const MaterialBindEntry*,
    Hasher<Name>,
    EqualTo<Name>,
    Core::Alloc::ScratchArena<>
>;

static void BuildMaterialBindInterfaceLookup(
    const ShaderCook::CookVector<MaterialBindEntry>& materialBindEntries,
    MaterialBindInterfaceLookup& outLookup
){
    outLookup.reserve(materialBindEntries.size());
    for(const MaterialBindEntry& bindEntry : materialBindEntries)
        outLookup.emplace(Name(bindEntry.virtualPath.c_str()), &bindEntry);
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
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' typed layout exceeds supported block count for '{}'")
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(contextName.c_str())
        );
        return false;
    }

    return true;
}

static bool BuildMaterialTypedLayoutDefaults(
    const MaterialBindEntry& bindEntry,
    const Name& contextName,
    Material::TypedLayoutBlockVector& outBlocks,
    Material::TypedLayoutFieldVector& outFields,
    Material::TypedBlockByteVector& outBlockBytes,
    u64& outLayoutHash
){
    outLayoutHash = 0u;
    outBlocks.clear();
    outFields.clear();
    outBlockBytes.clear();

    ScratchArena scratchArena;
    ScratchVector<const MaterialBindInstance*> sortedInstances{ scratchArena };
    if(!BuildSortedMaterialBindInstances(bindEntry, contextName, sortedInstances))
        return false;

    outBlocks.reserve(sortedInstances.size());
    for(const MaterialBindInstance* instance : sortedInstances){
        const MaterialBindStruct* bindStruct = bindEntry.findStruct(AStringView(instance->type));
        if(!bindStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' instance '{}' references unknown "
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
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' struct '{}' is missing a material block class for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(bindStruct->name)
                , StringConvert(contextName.c_str())
            );
            return false;
        }

        if(outFields.size() > Limit<u32>::s_Max || bindStruct->fields.size() > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' typed layout block '{}' exceeds "
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
        block.fieldBegin = static_cast<u32>(outFields.size());
        block.fieldCount = static_cast<u32>(bindStruct->fields.size());
        block.byteSize = 0u;

        if(!block.blockName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' has invalid typed layout block '{}' for '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance->name)
                , StringConvert(contextName.c_str())
            );
            return false;
        }

        outFields.reserve(outFields.size() + bindStruct->fields.size());
        for(const MaterialBindField& bindField : bindStruct->fields){
            MaterialLayoutFieldType::Enum fieldType = MaterialLayoutFieldType::None;
            if(!ParseMaterialLayoutFieldType(AStringView(bindField.type), fieldType)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' field '{}.{}' has unsupported "
                    "typed layout type '{}' for '{}'")
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
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' typed layout block '{}' exceeds "
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
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' has invalid typed layout field '{}.{}' for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextName.c_str())
                );
                return false;
            }
            if(!BuildMaterialTypedLayoutDefaultValue(contextName, *instance, bindField, fieldType, field.defaultValue))
                return false;
            if(!AppendMaterialTypedLayoutFieldBytes(outBlockBytes, fieldType, field.defaultValue)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' typed layout field '{}.{}' could "
                    "not append packed default bytes for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextName.c_str())
                );
                return false;
            }

            outFields.push_back(field);
            block.byteSize += fieldByteSize;
        }

        outBlocks.push_back(block);
    }

    outLayoutHash = MaterialBinaryPayload::ComputeMaterialTypedLayoutHash(
        outBlocks,
        outFields
    );
    if(outLayoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' produced an empty typed layout for '{}'")
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(contextName.c_str())
        );
        return false;
    }

    usize expectedBlockByteSize = 0u;
    if(!MaterialBinaryPayload::ComputeMaterialTypedBlockByteSize(outBlocks, expectedBlockByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' produced invalid packed typed block bytes for '{}'")
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(contextName.c_str())
        );
        return false;
    }
    if(expectedBlockByteSize != outBlockBytes.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface '{}' produced invalid packed typed block bytes for '{}'")
            , StringConvert(bindEntry.virtualPath)
            , StringConvert(contextName.c_str())
        );
        return false;
    }

    return true;
}

static void CopyMaterialTypedLayoutCache(
    const MaterialTypedLayoutCacheEntry& layoutCache,
    MaterialCookEntry& outMaterialEntry
){
    outMaterialEntry.typedLayoutHash = layoutCache.typedLayoutHash;
    outMaterialEntry.typedLayoutBlocks.assign(layoutCache.typedLayoutBlocks.begin(), layoutCache.typedLayoutBlocks.end());
    outMaterialEntry.typedLayoutFields.assign(layoutCache.typedLayoutFields.begin(), layoutCache.typedLayoutFields.end());
    outMaterialEntry.typedBlockBytes.assign(layoutCache.typedBlockBytes.begin(), layoutCache.typedBlockBytes.end());
}

static bool BuildMaterialTypedLayout(
    const MaterialTypedLayoutCacheEntry& layoutCache,
    MaterialCookEntry& outMaterialEntry
){
    if(!layoutCache.bindEntry){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' has no material bind layout cache")
            , StringConvert(outMaterialEntry.virtualPath.c_str())
        );
        return false;
    }

    const MaterialBindEntry& bindEntry = *layoutCache.bindEntry;
    CopyMaterialTypedLayoutCache(layoutCache, outMaterialEntry);
    if(outMaterialEntry.parameters.empty())
        return true;

    ScratchArena scratchArena;
    MaterialTypedLayoutBlockLookup blockLookup(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    if(!BuildMaterialTypedLayoutBlockLookup(
        layoutCache.typedLayoutBlocks,
        AStringView(outMaterialEntry.virtualPath.c_str()),
        blockLookup
    ))
        return false;

    MaterialTypedLayoutParameterLookup parameterLookup(
        0,
        Hasher<CompactString>(),
        EqualTo<CompactString>(),
        scratchArena
    );
    if(!BuildMaterialTypedLayoutParameterLookup(
        bindEntry,
        layoutCache.typedLayoutFields,
        blockLookup,
        AStringView(outMaterialEntry.virtualPath.c_str()),
        parameterLookup
    ))
        return false;

    for(const auto& [parameterName, parameterValue] : outMaterialEntry.parameters){
        if(!ApplyMaterialTypedLayoutParameterValue(bindEntry, parameterLookup, parameterName, parameterValue, outMaterialEntry))
            return false;
    }

    return true;
}

static bool FindOrBuildMaterialTypedLayoutCache(
    const Name& materialInterface,
    const MaterialBindEntry& bindEntry,
    ShaderCook::CookVector<MaterialTypedLayoutCacheEntry>& inOutCacheEntries,
    MaterialTypedLayoutCacheLookup& inOutCacheLookup,
    const MaterialTypedLayoutCacheEntry*& outCacheEntry
){
    outCacheEntry = nullptr;

    const auto cacheIt = inOutCacheLookup.find(materialInterface);
    if(cacheIt != inOutCacheLookup.end()){
        const usize cacheIndex = cacheIt.value();
        if(cacheIndex >= inOutCacheEntries.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: typed layout cache index is out of range for material interface '{}'")
                , StringConvert(materialInterface.c_str())
            );
            return false;
        }

        outCacheEntry = &inOutCacheEntries[cacheIndex];
        return true;
    }

    const usize cacheIndex = inOutCacheEntries.size();
    inOutCacheEntries.emplace_back(inOutCacheEntries.get_allocator().arena());
    MaterialTypedLayoutCacheEntry& cacheEntry = inOutCacheEntries.back();
    cacheEntry.bindEntry = &bindEntry;
    if(!BuildMaterialTypedLayoutDefaults(
        bindEntry,
        materialInterface,
        cacheEntry.typedLayoutBlocks,
        cacheEntry.typedLayoutFields,
        cacheEntry.typedBlockBytes,
        cacheEntry.typedLayoutHash
    ))
        return false;

    if(!inOutCacheLookup.emplace(materialInterface, cacheIndex).second){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: duplicate typed layout cache for material interface '{}'")
            , StringConvert(materialInterface.c_str())
        );
        return false;
    }

    outCacheEntry = &cacheEntry;
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// material bind generated Slang include helpers


static char ToGeneratedAsciiUpper(const char ch){
    return (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - ('a' - 'A')) : ch;
}

static bool IsGeneratedAlphaNumeric(const char ch){
    return
        (ch >= 'A' && ch <= 'Z')
        || (ch >= 'a' && ch <= 'z')
        || (ch >= '0' && ch <= '9')
    ;
}

static void AppendMaterialBindGeneratedSeparator(CookString& inOutSource, const u32 newlineCount){
    static constexpr AStringView s_SeparatorChunk = "////////////////";
    for(u32 i = 0u; i < 8u; ++i)
        inOutSource += s_SeparatorChunk;
    for(u32 i = 0u; i < newlineCount; ++i)
        inOutSource += '\n';
}

static void AppendGeneratedUpperIdentifier(const AStringView text, CookString& inOutText){
    const usize beginSize = inOutText.size();
    for(const char ch : text)
        inOutText += IsGeneratedAlphaNumeric(ch) ? ToGeneratedAsciiUpper(ch) : '_';
    if(inOutText.size() == beginSize)
        inOutText += "VALUE";
}

static void AppendGeneratedPascalIdentifier(const AStringView text, CookString& inOutText){
    const usize beginSize = inOutText.size();
    bool upperNext = true;
    for(const char ch : text){
        if(ch == '_'){
            upperNext = true;
            continue;
        }

        if(upperNext)
            inOutText += ToGeneratedAsciiUpper(ch);
        else
            inOutText += ch;
        upperNext = false;
    }
    if(inOutText.size() == beginSize)
        inOutText += "Value";
}

static void AppendHexU32Slang(const u32 value, CookString& inOutText){
    static constexpr char s_HexDigits[] = "0123456789abcdef";
    inOutText += "0x";
    for(u32 nibbleIndex = 0u; nibbleIndex < 8u; ++nibbleIndex){
        const u32 shift = (7u - nibbleIndex) * 4u;
        inOutText += s_HexDigits[(value >> shift) & 0xfu];
    }
    inOutText += 'u';
}

static void AppendU32Decimal(const u32 value, CookString& inOutText){
    char digits[10u];
    u32 digitCount = 0u;
    u32 remaining = value;
    do{
        digits[digitCount] = static_cast<char>('0' + (remaining % 10u));
        remaining /= 10u;
        ++digitCount;
    } while(remaining != 0u);

    while(digitCount > 0u){
        --digitCount;
        inOutText += digits[digitCount];
    }
}

static void AppendU32Slang(const u32 value, CookString& inOutText){
    AppendU32Decimal(value, inOutText);
    inOutText += 'u';
}

static void AppendU64AsUint2Slang(const u64 value, CookString& inOutText){
    inOutText += "uint2(";
    AppendHexU32Slang(static_cast<u32>(value & 0xffffffffull), inOutText);
    inOutText += ", ";
    AppendHexU32Slang(static_cast<u32>(value >> 32u), inOutText);
    inOutText += ")";
}

static CookString BuildMaterialBindIncludeGuard(ShaderCook::CookArena& arena, const AStringView includePath){
    CookString guard("NWB_GENERATED_MATERIAL_BIND_", arena);
    AppendGeneratedUpperIdentifier(AStringView(includePath), guard);
    return guard;
}

static CookString BuildMaterialBindGlobalSymbol(ShaderCook::CookArena& arena, const AStringView suffix){
    CookString symbol("NWB_MATERIAL_BIND_", arena);
    symbol += suffix;
    return symbol;
}

static CookString BuildMaterialBindBlockSymbol(
    ShaderCook::CookArena& arena,
    const AStringView blockName,
    const AStringView suffix
){
    CookString symbol("NWB_MATERIAL_BIND_", arena);
    AppendGeneratedUpperIdentifier(blockName, symbol);
    symbol += suffix;
    return symbol;
}

static CookString BuildMaterialBindFieldSymbol(
    ShaderCook::CookArena& arena,
    const MaterialBindInstance& instance,
    const MaterialBindField& field,
    const AStringView suffix
){
    CookString symbol("NWB_MATERIAL_BIND_", arena);
    AppendGeneratedUpperIdentifier(AStringView(instance.name), symbol);
    symbol += '_';
    AppendGeneratedUpperIdentifier(AStringView(field.name), symbol);
    symbol += suffix;
    return symbol;
}

static CookString BuildMaterialBindFieldAccessorName(
    ShaderCook::CookArena& arena,
    const MaterialBindInstance& instance,
    const MaterialBindField& field
){
    CookString functionName("nwbMaterialBindLoad", arena);
    AppendGeneratedPascalIdentifier(AStringView(instance.name), functionName);
    AppendGeneratedPascalIdentifier(AStringView(field.name), functionName);
    return functionName;
}

static CookString BuildMaterialBindBlockAccessorName(ShaderCook::CookArena& arena, const MaterialBindInstance& instance){
    CookString functionName("nwbMaterialBindLoad", arena);
    AppendGeneratedPascalIdentifier(AStringView(instance.name), functionName);
    return functionName;
}

static bool RegisterGeneratedMaterialBindSymbol(
    const AStringView includePath,
    const AStringView symbol,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena
){
    ScratchString scratchSymbol(symbol, scratchArena);
    if(inOutSymbols.insert(Move(scratchSymbol)).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': generated symbol '{}' is ambiguous")
        , StringConvert(includePath)
        , StringConvert(symbol)
    );
    return false;
}

static AStringView MaterialBindFieldLookupFunctionName(const MaterialLayoutFieldType::Enum fieldType){
    static constexpr AStringView s_FloatLookupNames[] = {
        "nwbMaterialLoadFloat",
        "nwbMaterialLoadFloat2",
        "nwbMaterialLoadFloat3",
        "nwbMaterialLoadFloat4"
    };
    static constexpr AStringView s_IntLookupNames[] = {
        "nwbMaterialLoadInt",
        "nwbMaterialLoadInt2",
        "nwbMaterialLoadInt3",
        "nwbMaterialLoadInt4"
    };
    static constexpr AStringView s_UIntLookupNames[] = {
        "nwbMaterialLoadUInt",
        "nwbMaterialLoadUInt2",
        "nwbMaterialLoadUInt3",
        "nwbMaterialLoadUInt4"
    };
    static constexpr AStringView s_BoolLookupNames[] = {
        "nwbMaterialLoadBool",
        "nwbMaterialLoadBool2",
        "nwbMaterialLoadBool3",
        "nwbMaterialLoadBool4"
    };

    const u32 componentCount = MaterialLayoutFieldComponentCount(fieldType);
    if(componentCount == 0u || componentCount > 4u)
        return AStringView();

    switch(MaterialLayoutFieldValueType(fieldType)){
    case MaterialParameterValueType::Float: return s_FloatLookupNames[componentCount - 1u];
    case MaterialParameterValueType::Int: return s_IntLookupNames[componentCount - 1u];
    case MaterialParameterValueType::UInt: return s_UIntLookupNames[componentCount - 1u];
    case MaterialParameterValueType::Bool: return s_BoolLookupNames[componentCount - 1u];
    default: return AStringView();
    }
}

static bool AppendMaterialBindU32Constant(
    const AStringView includePath,
    const CookString& symbol,
    const u32 value,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(symbol), inOutSymbols, scratchArena))
        return false;

    inOutSource += "static const uint ";
    inOutSource += symbol;
    inOutSource += " = ";
    AppendU32Slang(value, inOutSource);
    inOutSource += ";\n";
    return true;
}

static bool AppendMaterialBindU64Constant(
    const AStringView includePath,
    const CookString& symbol,
    const u64 value,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(symbol), inOutSymbols, scratchArena))
        return false;

    inOutSource += "static const uint2 ";
    inOutSource += symbol;
    inOutSource += " = ";
    AppendU64AsUint2Slang(value, inOutSource);
    inOutSource += ";\n";
    return true;
}

static bool AppendMaterialBindLayoutConstants(
    ShaderCook::CookArena& arena,
    const AStringView includePath,
    const MaterialBindEntry& entry,
    const Material::TypedLayoutBlockVector& blocks,
    const Material::TypedLayoutFieldVector& fields,
    const MaterialTypedLayoutBlockLookup& blockLookup,
    const u64 layoutHash,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    usize totalBlockByteSize = 0u;
    if(!MaterialBinaryPayload::ComputeMaterialTypedBlockByteSize(blocks, totalBlockByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout byte size exceeds u32")
            , StringConvert(includePath)
        );
        return false;
    }
    if(totalBlockByteSize > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout byte size exceeds u32")
            , StringConvert(includePath)
        );
        return false;
    }

    const Name interfaceName(AStringView(entry.virtualPath));
    const NameHash& interfaceHash = interfaceName.hash();
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane){
        CookString laneSuffix("INTERFACE_HASH_", arena);
        AppendU32Decimal(lane, laneSuffix);
        const CookString symbol = BuildMaterialBindGlobalSymbol(arena, AStringView(laneSuffix));
        if(!AppendMaterialBindU64Constant(
            includePath,
            symbol,
            interfaceHash.qwords[lane],
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
    }

    const CookString layoutHashSymbol = BuildMaterialBindGlobalSymbol(arena, "LAYOUT_HASH");
    const CookString blockCountSymbol = BuildMaterialBindGlobalSymbol(arena, "BLOCK_COUNT");
    const CookString fieldCountSymbol = BuildMaterialBindGlobalSymbol(arena, "FIELD_COUNT");
    const CookString blockByteSizeSymbol = BuildMaterialBindGlobalSymbol(arena, "BLOCK_BYTE_SIZE");

    if(!AppendMaterialBindU64Constant(includePath, layoutHashSymbol, layoutHash, inOutSymbols, scratchArena, inOutSource))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        blockCountSymbol,
        static_cast<u32>(blocks.size()),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        fieldCountSymbol,
        static_cast<u32>(fields.size()),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        blockByteSizeSymbol,
        static_cast<u32>(totalBlockByteSize),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;

    ScratchVector<const MaterialBindInstance*> sortedInstances{ scratchArena };
    if(!BuildSortedMaterialBindInstances(entry, Name(AStringView(entry.virtualPath)), sortedInstances))
        return false;
    if(sortedInstances.size() != blocks.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout block count mismatch"), StringConvert(includePath));
        return false;
    }

    for(const MaterialBindInstance* instance : sortedInstances){
        const auto blockIt = blockLookup.find(Name(AStringView(instance->name)));
        if(blockIt == blockLookup.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' has no typed layout block")
                , StringConvert(includePath)
                , StringConvert(instance->name)
            );
            return false;
        }

        const MaterialTypedLayoutBlockLookupEntry& blockEntry = blockIt.value();
        const MaterialTypedLayoutBlock& block = *blockEntry.block;
        const CookString offsetSymbol = BuildMaterialBindBlockSymbol(arena, AStringView(instance->name), "_BLOCK_BYTE_OFFSET");
        const CookString sizeSymbol = BuildMaterialBindBlockSymbol(arena, AStringView(instance->name), "_BLOCK_BYTE_SIZE");
        if(!AppendMaterialBindU32Constant(
            includePath,
            offsetSymbol,
            blockEntry.byteBegin,
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
        if(!AppendMaterialBindU32Constant(includePath, sizeSymbol, block.byteSize, inOutSymbols, scratchArena, inOutSource))
            return false;
    }

    return true;
}

static bool AppendMaterialBindFieldConstants(
    ShaderCook::CookArena& arena,
    const AStringView includePath,
    const MaterialBindStruct& bindStruct,
    const MaterialBindInstance& instance,
    const MaterialBindField& field,
    const CookString& keySymbol,
    const CookString& defaultSymbol,
    CookString& inOutSource
){
    const AStringView defaultAttribute = field.defaultArgument();
    if(defaultAttribute.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' must declare a default attribute")
            , StringConvert(includePath)
            , StringConvert(bindStruct.name)
            , StringConvert(field.name)
        );
        return false;
    }

    CookString keyText(instance.name, arena);
    keyText += '.';
    keyText += field.name;
    const u64 keyHash = UpdateFnv64TextCanonical(FNV64_OFFSET_BASIS, AStringView(keyText));

    inOutSource += "static const uint2 ";
    inOutSource += keySymbol;
    inOutSource += " = uint2(";
    AppendHexU32Slang(static_cast<u32>(keyHash & 0xffffffffull), inOutSource);
    inOutSource += ", ";
    AppendHexU32Slang(static_cast<u32>(keyHash >> 32u), inOutSource);
    inOutSource += ");\n";

    inOutSource += "static const ";
    inOutSource += field.type;
    inOutSource += ' ';
    inOutSource += defaultSymbol;
    inOutSource += " = ";
    inOutSource += defaultAttribute;
    inOutSource += ";\n";
    return true;
}

static void AppendMaterialBindFieldAccessor(
    const MaterialBindField& field,
    const CookString& byteOffsetSymbol,
    const CookString& functionName,
    const AStringView loadFunctionName,
    CookString& inOutSource
){
    inOutSource += field.type;
    inOutSource += ' ';
    inOutSource += functionName;
    inOutSource += "(const NwbMeshInstanceData instance){\n";
    inOutSource += "    return ";
    inOutSource += loadFunctionName;
    inOutSource += "(instance, ";
    inOutSource += byteOffsetSymbol;
    inOutSource += ");\n";
    inOutSource += "}\n\n";
    inOutSource += field.type;
    inOutSource += ' ';
    inOutSource += functionName;
    inOutSource += "(){\n";
    inOutSource += "    return ";
    inOutSource += functionName;
    inOutSource += "(nwbMeshLoadInstance());\n";
    inOutSource += "}\n";
}

static bool AppendMaterialBindGeneratedInstance(
    ShaderCook::CookArena& arena,
    const AStringView includePath,
    const MaterialBindInstance& instance,
    const MaterialBindStruct& bindStruct,
    const Material::TypedLayoutFieldVector& layoutFields,
    const MaterialTypedLayoutBlockLookup& layoutBlockLookup,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    inOutSource += "\n";
    AppendMaterialBindGeneratedSeparator(inOutSource, 3u);

    const auto blockIt = layoutBlockLookup.find(Name(AStringView(instance.name)));
    if(blockIt == layoutBlockLookup.end()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' has no typed layout block")
            , StringConvert(includePath)
            , StringConvert(instance.name)
        );
        return false;
    }
    const MaterialTypedLayoutBlockLookupEntry& layoutBlockEntry = blockIt.value();
    const MaterialTypedLayoutBlock* layoutBlock = layoutBlockEntry.block;
    if(layoutBlock->fieldCount != bindStruct.fields.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' typed layout field count mismatch")
            , StringConvert(includePath)
            , StringConvert(instance.name)
        );
        return false;
    }

    for(u32 fieldOffset = 0u; fieldOffset < layoutBlock->fieldCount; ++fieldOffset){
        const usize layoutFieldIndex = static_cast<usize>(layoutBlock->fieldBegin) + fieldOffset;
        if(layoutFieldIndex >= layoutFields.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' typed layout field range exceeds layout")
                , StringConvert(includePath)
                , StringConvert(instance.name)
            );
            return false;
        }

        const MaterialBindField& field = bindStruct.fields[fieldOffset];
        const MaterialTypedLayoutField& layoutField = layoutFields[layoutFieldIndex];
        if(layoutField.fieldName != Name(AStringView(field.name))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' typed layout metadata mismatch")
                , StringConvert(includePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }
        if(layoutField.offset > Limit<u32>::s_Max - layoutBlockEntry.byteBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' byte offset exceeds u32")
                , StringConvert(includePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }

        const AStringView loadFunctionName = MaterialBindFieldLookupFunctionName(layoutField.fieldType);
        if(loadFunctionName.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' has unsupported load type '{}'")
                , StringConvert(includePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
                , StringConvert(field.type)
            );
            return false;
        }

        const CookString keySymbol = BuildMaterialBindFieldSymbol(arena, instance, field, "_KEY");
        const CookString defaultSymbol = BuildMaterialBindFieldSymbol(arena, instance, field, "_DEFAULT");
        const CookString byteOffsetSymbol = BuildMaterialBindFieldSymbol(arena, instance, field, "_BYTE_OFFSET");
        const CookString functionName = BuildMaterialBindFieldAccessorName(arena, instance, field);
        if(!RegisterGeneratedMaterialBindSymbol(
            includePath,
            AStringView(keySymbol),
            inOutSymbols,
            scratchArena
        ))
            return false;
        if(!RegisterGeneratedMaterialBindSymbol(
            includePath,
            AStringView(defaultSymbol),
            inOutSymbols,
            scratchArena
        ))
            return false;
        if(!RegisterGeneratedMaterialBindSymbol(
            includePath,
            AStringView(byteOffsetSymbol),
            inOutSymbols,
            scratchArena
        ))
            return false;
        if(!RegisterGeneratedMaterialBindSymbol(
            includePath,
            AStringView(functionName),
            inOutSymbols,
            scratchArena
        ))
            return false;

        if(!AppendMaterialBindFieldConstants(
            arena,
            includePath,
            bindStruct,
            instance,
            field,
            keySymbol,
            defaultSymbol,
            inOutSource
        ))
            return false;
        const u32 fieldByteOffset = layoutBlockEntry.byteBegin + layoutField.offset;
        inOutSource += "static const uint ";
        inOutSource += byteOffsetSymbol;
        inOutSource += " = ";
        AppendU32Slang(fieldByteOffset, inOutSource);
        inOutSource += ";\n";
        inOutSource += '\n';
        AppendMaterialBindFieldAccessor(field, byteOffsetSymbol, functionName, loadFunctionName, inOutSource);
        inOutSource += '\n';
    }

    const CookString blockFunctionName = BuildMaterialBindBlockAccessorName(arena, instance);
    if(!RegisterGeneratedMaterialBindSymbol(
        includePath,
        AStringView(blockFunctionName),
        inOutSymbols,
        scratchArena
    ))
        return false;

    inOutSource += bindStruct.name;
    inOutSource += ' ';
    inOutSource += blockFunctionName;
    inOutSource += "(const NwbMeshInstanceData instance){\n";
    inOutSource += "    ";
    inOutSource += bindStruct.name;
    inOutSource += " value;\n";
    for(const MaterialBindField& field : bindStruct.fields){
        const CookString functionName = BuildMaterialBindFieldAccessorName(arena, instance, field);
        inOutSource += "    value.";
        inOutSource += field.name;
        inOutSource += " = ";
        inOutSource += functionName;
        inOutSource += "(instance);\n";
    }
    inOutSource += "    return value;\n";
    inOutSource += "}\n\n";
    inOutSource += bindStruct.name;
    inOutSource += ' ';
    inOutSource += blockFunctionName;
    inOutSource += "(){\n";
    inOutSource += "    return ";
    inOutSource += blockFunctionName;
    inOutSource += "(nwbMeshLoadInstance());\n";
    inOutSource += "}\n";

    return true;
}

static bool BuildMaterialBindIncludeSourceImpl(
    ShaderCook::CookArena& arena,
    const MaterialBindEntry& entry,
    CookString& outSource
){
    outSource.clear();

    CookString includePath(arena);
    if(!BuildMaterialBindIncludeVirtualPathImpl(arena, entry, includePath))
        return false;

    const CookString includeGuard = BuildMaterialBindIncludeGuard(arena, AStringView(includePath));

    Material::TypedLayoutBlockVector layoutBlocks(arena);
    Material::TypedLayoutFieldVector layoutFields(arena);
    Material::TypedBlockByteVector layoutBytes(arena);
    u64 layoutHash = 0u;
    if(!BuildMaterialTypedLayoutDefaults(
        entry,
        Name(AStringView(entry.virtualPath)),
        layoutBlocks,
        layoutFields,
        layoutBytes,
        layoutHash
    ))
        return false;

    ScratchArena scratchArena;
    MaterialTypedLayoutBlockLookup layoutBlockLookup(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    if(!BuildMaterialTypedLayoutBlockLookup(layoutBlocks, AStringView(includePath), layoutBlockLookup))
        return false;

    ScratchHashSet<ScratchString> generatedSymbols{
        0,
        Hasher<ScratchString>(),
        EqualTo<ScratchString>(),
        scratchArena
    };

    outSource += "// generated by NWBLot material bind cook\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);
    outSource += "#ifndef ";
    outSource += includeGuard;
    outSource += "\n#define ";
    outSource += includeGuard;
    outSource += "\n\n\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);
    outSource += "#ifndef NWB_MATERIAL_TYPED_BINDING\n";
    outSource += "#error \"generated material bind includes require mesh_shader_authoring.slangi\"\n";
    outSource += "#endif\n\n";
    outSource += "#if NWB_MATERIAL_TYPED_BINDING != 1\n";
    outSource += "#error \"generated material bind accessors require NWB_MATERIAL_TYPED_BINDING=1\"\n";
    outSource += "#endif\n\n\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);

    if(!AppendMaterialBindLayoutConstants(
        arena,
        AStringView(includePath),
        entry,
        layoutBlocks,
        layoutFields,
        layoutBlockLookup,
        layoutHash,
        generatedSymbols,
        scratchArena,
        outSource
    ))
        return false;

    outSource += "\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);

    for(const MaterialBindStruct& bindStruct : entry.structs){
        if(!RegisterGeneratedMaterialBindSymbol(
            AStringView(includePath),
            AStringView(bindStruct.name),
            generatedSymbols,
            scratchArena
        ))
            return false;

        outSource += "struct ";
        outSource += bindStruct.name;
        outSource += "{\n";
        for(const MaterialBindField& field : bindStruct.fields){
            outSource += "    ";
            outSource += field.type;
            outSource += ' ';
            outSource += field.name;
            outSource += ";\n";
        }
        outSource += "};\n\n";
    }

    for(const MaterialBindInstance& instance : entry.instances){
        const MaterialBindStruct* bindStruct = entry.findStruct(AStringView(instance.type));
        if(!bindStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' references unknown struct type '{}'")
                , StringConvert(includePath)
                , StringConvert(instance.name)
                , StringConvert(instance.type)
            );
            return false;
        }

        if(!AppendMaterialBindGeneratedInstance(
            arena,
            AStringView(includePath),
            instance,
            *bindStruct,
            layoutFields,
            layoutBlockLookup,
            generatedSymbols,
            scratchArena,
            outSource
        ))
            return false;
    }

    outSource += "\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);
    outSource += "#endif\n\n\n";
    AppendMaterialBindGeneratedSeparator(outSource, 1u);
    return true;
}

static bool PrepareMaterialBindIncludeRoot(const Path& includeRoot){
    ErrorCode errorCode;
    if(!RemoveAllIfExists(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to clear generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    errorCode.clear();
    if(!EnsureDirectories(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to create generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}

static bool EmitMaterialBindIncludes(
    ShaderCook::CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const ShaderCook::CookVector<MaterialBindEntry>& materialBindEntries,
    Path& outIncludeRoot
){
    outIncludeRoot.clear();
    if(materialBindEntries.empty())
        return true;

    outIncludeRoot = BuildMaterialBindIncludeRoot(cacheDirectory, configurationSafeName);
    if(!PrepareMaterialBindIncludeRoot(outIncludeRoot))
        return false;

    ShaderCook::CookHashSet<CookString> seenIncludePaths{arena};
    seenIncludePaths.reserve(materialBindEntries.size());

    for(const MaterialBindEntry& bindEntry : materialBindEntries){
        CookString includePath(arena);
        if(!BuildMaterialBindIncludeVirtualPathImpl(arena, bindEntry, includePath))
            return false;
        if(!seenIncludePaths.insert(includePath).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: duplicate material bind include path '{}'")
                , StringConvert(includePath)
            );
            return false;
        }

        CookString generatedSource{arena};
        if(!BuildMaterialBindIncludeSourceImpl(arena, bindEntry, generatedSource))
            return false;

        const Path outputPath = outIncludeRoot / Path(includePath.c_str());
        ErrorCode errorCode;
        if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to create generated include parent '{}': {}")
                , PathToString<tchar>(outputPath.parent_path())
                , StringConvert(errorCode.message())
            );
            return false;
        }

        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to write generated include '{}'")
                , PathToString<tchar>(outputPath)
            );
            return false;
        }
    }

    return true;
}


static bool ValidateMaterialCookInterfaces(
    const ShaderCook::CookVector<MaterialBindEntry>& materialBindEntries,
    ShaderCook::CookVector<MaterialCookEntry>& materialEntries
){
    Core::Alloc::ScratchArena<> scratchArena;
    MaterialBindInterfaceLookup materialBindLookup(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    BuildMaterialBindInterfaceLookup(materialBindEntries, materialBindLookup);

    const usize cacheReserveCount = Min(materialBindEntries.size(), materialEntries.size());
    ShaderCook::CookVector<MaterialTypedLayoutCacheEntry> layoutCacheEntries(materialEntries.get_allocator().arena());
    layoutCacheEntries.reserve(cacheReserveCount);

    MaterialTypedLayoutCacheLookup layoutCacheLookup(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    layoutCacheLookup.reserve(cacheReserveCount);

    for(MaterialCookEntry& materialEntry : materialEntries){
        materialEntry.typedLayoutHash = 0u;
        materialEntry.typedLayoutBlocks.clear();
        materialEntry.typedLayoutFields.clear();
        materialEntry.typedBlockBytes.clear();

        if(!materialEntry.materialInterface){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' is missing required material interface")
                , StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }

        const auto bindEntryIt = materialBindLookup.find(materialEntry.materialInterface);
        if(bindEntryIt == materialBindLookup.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' references unknown material interface '{}'")
                , StringConvert(materialEntry.virtualPath.c_str())
                , StringConvert(materialEntry.materialInterface.c_str())
            );
            return false;
        }
        const MaterialBindEntry* bindEntry = bindEntryIt.value();

        const MaterialTypedLayoutCacheEntry* layoutCache = nullptr;
        if(!FindOrBuildMaterialTypedLayoutCache(
            materialEntry.materialInterface,
            *bindEntry,
            layoutCacheEntries,
            layoutCacheLookup,
            layoutCache
        ))
            return false;
        if(!layoutCache){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' failed to resolve typed layout cache for interface '{}'")
                , StringConvert(materialEntry.virtualPath.c_str())
                , StringConvert(materialEntry.materialInterface.c_str())
            );
            return false;
        }

        if(!BuildMaterialTypedLayout(*layoutCache, materialEntry))
            return false;
    }

    return true;
}

static bool ParseMaterialMeta(
    ShaderCook& shaderCook,
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MaterialCookEntry& outEntry
){
    outEntry.reset();

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': asset is not a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        asset,
        "Material",
        outEntry.virtualPath
    ))
        return false;
    if(!ValidateMaterialAssetFields(nwbFilePath, asset))
        return false;

    if(!ParseVariantField(shaderCook, nwbFilePath, asset, "shader_variant", outEntry.shaderVariant))
        return false;
    if(!ParseMaterialInterface(nwbFilePath, asset, outEntry.materialInterface))
        return false;
    if(!ParseMaterialRenderProperties(nwbFilePath, asset, outEntry))
        return false;
    if(!ParseMaterialStageShaders(nwbFilePath, asset, outEntry.stageShaders))
        return false;
    if(!ParseMaterialParameters(nwbFilePath, asset, outEntry.parameters))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseMaterialCookMetadata(
    ShaderCook& shaderCook,
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MaterialCookEntry& outEntry
){
    return __hidden_material_asset::ParseMaterialMeta(shaderCook, assetRoot, virtualRoot, nwbFilePath, doc, outEntry);
}

bool ValidateMaterialCookInterfaces(
    const ShaderCook::CookVector<MaterialBindEntry>& materialBindEntries,
    ShaderCook::CookVector<MaterialCookEntry>& materialEntries
){
    return __hidden_material_asset::ValidateMaterialCookInterfaces(materialBindEntries, materialEntries);
}

bool BuildMaterialBindIncludeSource(
    ShaderCook::CookArena& arena,
    const MaterialBindEntry& entry,
    ShaderCook::CookString& outSource
){
    return __hidden_material_asset::BuildMaterialBindIncludeSourceImpl(arena, entry, outSource);
}

bool EmitMaterialBindIncludes(
    ShaderCook::CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const ShaderCook::CookVector<MaterialBindEntry>& materialBindEntries,
    Path& outIncludeRoot
){
    return __hidden_material_asset::EmitMaterialBindIncludes(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialBindEntries,
        outIncludeRoot
    );
}

bool ResolveMaterialBindDependencyInterface(
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const ShaderCook::CookVector<Path>& dependencies,
    ShaderCook::CookString& outInterfacePath,
    Name& outInterfaceName
){
    return __hidden_material_asset::ResolveMaterialBindDependencyInterface(
        shaderName,
        materialBindIncludeRoot,
        dependencies,
        outInterfacePath,
        outInterfaceName
    );
}

bool BuildMaterialAsset(const MaterialCookEntry& materialEntry, Material& outMaterial){
    Core::Assets::AssetArena& arena = materialEntry.shaderVariant.get_allocator().arena();
    if(!materialEntry.materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' is missing required material interface")
            , StringConvert(materialEntry.virtualPath.c_str())
        );
        return false;
    }
    if(materialEntry.typedLayoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface material '{}' is missing typed layout data")
            , StringConvert(materialEntry.virtualPath.c_str())
        );
        return false;
    }
    if(materialEntry.shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' has empty shader variant")
            , StringConvert(materialEntry.virtualPath.c_str())
        );
        return false;
    }

    outMaterial = Material(arena, materialEntry.virtualPath);
    outMaterial.setShaderVariant(materialEntry.shaderVariant);
    outMaterial.setMaterialInterface(materialEntry.materialInterface);
    outMaterial.setRenderProperties(materialEntry.alpha, materialEntry.transparent);
    outMaterial.setTypedLayout(
        materialEntry.typedLayoutHash,
        materialEntry.typedLayoutBlocks,
        materialEntry.typedLayoutFields,
        materialEntry.typedBlockBytes
    );

    for(const auto& [shaderType, shaderAsset] : materialEntry.stageShaders){
        if(!outMaterial.setShaderForStage(shaderType, shaderAsset)){
            const Name& stageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: invalid shader stage '{}' for '{}'")
                , StringConvert(stageName.c_str())
                , StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }
    }

    return true;
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
    if(!material.materialInterface()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: material interface is required"));
        return false;
    }
    if(material.shaderVariant().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader variant is empty"));
        return false;
    }
    if(material.typedLayoutHash() == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: interface material is missing typed layout data"));
        return false;
    }
    if(material.typedLayoutBlocks().size() > Limit<u32>::s_Max || material.typedLayoutFields().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed layout count exceeds u32 range"));
        return false;
    }
    if(material.typedBlockBytes().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed block byte count exceeds u32 range"));
        return false;
    }
    if(MaterialBinaryPayload::ComputeMaterialTypedLayoutHash(
        material.typedLayoutBlocks(),
        material.typedLayoutFields()
    ) != material.typedLayoutHash()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed layout hash mismatch"));
        return false;
    }
    usize expectedTypedBlockByteSize = 0u;
    if(!MaterialBinaryPayload::ComputeMaterialTypedBlockByteSize(
        material.typedLayoutBlocks(),
        expectedTypedBlockByteSize
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed block bytes do not match typed layout"));
        return false;
    }
    if(expectedTypedBlockByteSize != material.typedBlockBytes().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed block bytes do not match typed layout"));
        return false;
    }
    if(!IsFinite(material.alpha()) || material.alpha() < 0.0f || material.alpha() > 1.0f){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: material alpha must be in the [0, 1] range"));
        return false;
    }

    usize reserveBytes =
        sizeof(u32) + // magic
        sizeof(u32)   // version
    ;
    bool canReserve = AddBinaryStringReserveBytes(reserveBytes, AStringView(material.shaderVariant()))
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u64))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(
            reserveBytes,
            material.typedLayoutBlocks().size(),
            MaterialBinaryPayload::s_TypedLayoutBlockBytes
        )
        && AddBinaryRepeatedReserveBytes(
            reserveBytes,
            material.typedLayoutFields().size(),
            MaterialBinaryPayload::s_TypedLayoutFieldBytes
        )
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, material.typedBlockBytes().size())
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(reserveBytes, material.stageShaderCount(), MaterialBinaryPayload::s_ShaderEntryBytes)
        && AddBinaryReserveBytes(reserveBytes, sizeof(f32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
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
    AppendPOD(outBinary, material.materialInterface().hash());
    AppendPOD(outBinary, material.typedLayoutHash());
    AppendPOD(outBinary, static_cast<u32>(material.typedLayoutBlocks().size()));
    AppendPOD(outBinary, static_cast<u32>(material.typedLayoutFields().size()));
    for(const MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        MaterialBinaryPayload::MaterialTypedLayoutBlockBinary blockBinary;
        blockBinary.blockNameHash = block.blockName.hash();
        blockBinary.blockClass = static_cast<u32>(block.blockClass);
        blockBinary.fieldBegin = block.fieldBegin;
        blockBinary.fieldCount = block.fieldCount;
        blockBinary.byteSize = block.byteSize;
        AppendPOD(outBinary, blockBinary);
    }
    for(const MaterialTypedLayoutField& field : material.typedLayoutFields()){
        MaterialBinaryPayload::MaterialTypedLayoutFieldBinary fieldBinary;
        fieldBinary.fieldNameHash = field.fieldName.hash();
        fieldBinary.fieldType = static_cast<u32>(field.fieldType);
        fieldBinary.offset = field.offset;
        fieldBinary.defaultValue = field.defaultValue;
        AppendPOD(outBinary, fieldBinary);
    }
    AppendPOD(outBinary, static_cast<u32>(material.typedBlockBytes().size()));
    BinaryDetail::AppendBytesNoReserveUnchecked(
        outBinary,
        material.typedBlockBytes().data(),
        material.typedBlockBytes().size()
    );
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

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

