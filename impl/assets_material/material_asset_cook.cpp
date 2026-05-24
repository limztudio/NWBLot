// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_asset_cook.h"
#include "material_asset_metadata.h"
#include "material_binary_payload.h"

#include <impl/assets_shader/shader_cook.h>

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


using CookArena = MaterialCookArena;
using CookString = MaterialCookString;
using ScratchArena = Core::Alloc::ScratchArena;
using ScratchString = AString<ScratchArena>;
template<typename T>
using CookVector = MaterialCookVector<T>;
template<typename T>
using ScratchHashSet = HashSet<T, Hasher<T>, EqualTo<T>, ScratchArena>;
template<typename T>
using CookHashSet = MaterialCookHashSet<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Path BuildMaterialBindIncludeRoot(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    ScratchArena& scratchArena
){
    ScratchString configurationName(configurationSafeName, scratchArena);
    ScratchString includeDirectoryName(MaterialBindNames::GeneratedIncludeCacheDirectoryText(), scratchArena);
    return cacheDirectory / configurationName.c_str() / includeDirectoryName.c_str();
}

static bool BuildMaterialBindIncludeVirtualPathImpl(
    CookArena& arena,
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
    CookString& outInterfacePath,
    ScratchArena& scratchArena
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
    const CookVector<Path>& dependencies,
    CookString& outInterfacePath,
    Name& outInterfaceName,
    ScratchArena& scratchArena
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

    CookArena& arena = outInterfacePath.get_allocator().arena();
    CookString dependencyInterfacePath{arena};
    for(const Path& dependency : dependencies){
        if(!TryResolveMaterialBindDependencyInterface(
            normalizedMaterialBindIncludeRoot,
            dependency,
            dependencyInterfacePath,
            scratchArena
        ))
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


static bool ParseVariantField(
    ShaderCook& shaderCook,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    CookString& outVariant,
    ScratchArena& scratchArena
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

    CookString rawVariant{arena};
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

    CookString canonicalVariant{arena};
    if(!shaderCook.canonicalizeVariantSignature(rawVariantView, canonicalVariant, scratchArena)){
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

    const auto* shadersValue = asset.findField(MaterialAssetMetadataSchema::s_ShadersField);
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
        if(shaderType != Core::ShaderType::PixelStage && shaderType != Core::ShaderType::MeshStage){
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

    const auto* parametersValue = asset.findField(MaterialAssetMetadataSchema::s_ParametersField);
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

        ACompactString key;
        ACompactString value;
        const AStringView paramValueText(paramValue.asString().data(), paramValue.asString().size());
        if(!key.assign(paramKeyText) || !value.assign(paramValueText)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}' exceeds ACompactString capacity")
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

            ACompactString flattenedKey;
            if(!flattenedKey.assign(paramKeyText) || !flattenedKey.pushBack('.') || !flattenedKey.append(blockParamKeyText)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}.{}' exceeds ACompactString capacity")
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

    const auto* interfaceValue = asset.findField(MaterialAssetMetadataSchema::s_InterfaceField);
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

static bool ParseMaterialTransparentProperty(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    bool& outTransparent
){
    outTransparent = false;

    const auto* transparentValue = asset.findField(MaterialAssetMetadataSchema::s_TransparentField);
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
    if(!ParseMaterialTransparentProperty(nwbFilePath, asset, outEntry.transparent))
        return false;

    return true;
}

using MaterialBindInterfaceLookup = HashMap<
    Name,
    const MaterialBindEntry*,
    Hasher<Name>,
    EqualTo<Name>,
    Core::Alloc::ScratchArena
>;

static void BuildMaterialBindInterfaceLookup(
    const CookVector<MaterialBindEntry>& materialBindEntries,
    MaterialBindInterfaceLookup& outLookup
){
    outLookup.reserve(materialBindEntries.size());
    for(const MaterialBindEntry& bindEntry : materialBindEntries)
        outLookup.emplace(Name(bindEntry.virtualPath.c_str()), &bindEntry);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// material bind generated Slang include helpers


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
        inOutText += IsAsciiAlphaNumeric(ch) ? ToAsciiUpper(ch) : '_';
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
            inOutText += ToAsciiUpper(ch);
        else
            inOutText += ch;
        upperNext = false;
    }
    if(inOutText.size() == beginSize)
        inOutText += "Value";
}

static void AppendU32Slang(const u32 value, CookString& inOutText){
    char digits[16u];
    inOutText += FormatDecimal(static_cast<usize>(value), digits);
    inOutText += 'u';
}

static void AppendU64AsUint2Slang(const u64 value, CookString& inOutText){
    inOutText += "uint2(";
    AppendHexU32UnsignedLiteral(static_cast<u32>(value & 0xffffffffull), inOutText);
    inOutText += ", ";
    AppendHexU32UnsignedLiteral(static_cast<u32>(value >> 32u), inOutText);
    inOutText += ")";
}

static CookString BuildMaterialBindIncludeGuard(CookArena& arena, const AStringView includePath){
    CookString guard("NWB_GENERATED_MATERIAL_BIND_", arena);
    AppendGeneratedUpperIdentifier(AStringView(includePath), guard);
    return guard;
}

static CookString BuildMaterialBindGeneratedSymbol(
    CookArena& arena,
    const InitializerList<AStringView> nameSegments,
    const AStringView suffix
){
    CookString symbol("NWB_MATERIAL_BIND_", arena);
    bool firstSegment = true;
    for(const AStringView nameSegment : nameSegments){
        if(!firstSegment)
            symbol += '_';
        AppendGeneratedUpperIdentifier(nameSegment, symbol);
        firstSegment = false;
    }
    symbol += suffix;
    return symbol;
}

static CookString BuildMaterialBindAccessorName(
    CookArena& arena,
    const InitializerList<AStringView> nameSegments
){
    CookString functionName("nwbMaterialBindLoad", arena);
    for(const AStringView nameSegment : nameSegments)
        AppendGeneratedPascalIdentifier(nameSegment, functionName);
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

static bool AppendMaterialBindConstantPrefix(
    const AStringView includePath,
    const CookString& symbol,
    const AStringView type,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(symbol), inOutSymbols, scratchArena))
        return false;

    inOutSource += "static const ";
    inOutSource += type;
    inOutSource += ' ';
    inOutSource += symbol;
    inOutSource += " = ";
    return true;
}

static void AppendMaterialBindConstantSuffix(CookString& inOutSource){
    inOutSource += ";\n";
}

static bool AppendMaterialBindU32Constant(
    const AStringView includePath,
    const CookString& symbol,
    const u32 value,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    if(!AppendMaterialBindConstantPrefix(includePath, symbol, "uint", inOutSymbols, scratchArena, inOutSource))
        return false;

    AppendU32Slang(value, inOutSource);
    AppendMaterialBindConstantSuffix(inOutSource);
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
    if(!AppendMaterialBindConstantPrefix(includePath, symbol, "uint2", inOutSymbols, scratchArena, inOutSource))
        return false;

    AppendU64AsUint2Slang(value, inOutSource);
    AppendMaterialBindConstantSuffix(inOutSource);
    return true;
}

static bool ResolveMaterialBindGeneratedLayoutBlock(
    const AStringView includePath,
    const MaterialBindInstance& instance,
    const MaterialBindTypedLayout& layout,
    MaterialBindTypedLayoutBlockLookupEntry& outBlockEntry,
    const MaterialTypedLayoutBlock*& outBlock
){
    outBlockEntry = {};
    outBlock = nullptr;

    const auto blockIt = layout.blockLookup.find(Name(AStringView(instance.name)));
    if(blockIt == layout.blockLookup.end()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' has no typed layout block")
            , StringConvert(includePath)
            , StringConvert(instance.name)
        );
        return false;
    }

    outBlockEntry = blockIt.value();
    if(outBlockEntry.blockIndex >= layout.typedLayoutBlocks.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' typed layout block index is out of range")
            , StringConvert(includePath)
            , StringConvert(instance.name)
        );
        return false;
    }

    outBlock = &layout.typedLayoutBlocks[outBlockEntry.blockIndex];
    return true;
}

static bool AppendMaterialBindLayoutConstants(
    CookArena& arena,
    const AStringView includePath,
    const MaterialBindEntry& entry,
    const MaterialBindTypedLayout& layout,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    usize totalBlockByteSize = 0u;
    if(!MaterialBinaryPayload::ComputeMaterialTypedBlockByteSize(layout.typedLayoutBlocks, totalBlockByteSize)){
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
        char laneDigits[16u];
        laneSuffix += FormatDecimal(static_cast<usize>(lane), laneDigits);
        const CookString symbol = BuildMaterialBindGeneratedSymbol(arena, {}, AStringView(laneSuffix));
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

    const CookString layoutHashSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "LAYOUT_HASH");
    const CookString blockCountSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "BLOCK_COUNT");
    const CookString fieldCountSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "FIELD_COUNT");
    const CookString blockByteSizeSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "BLOCK_BYTE_SIZE");

    if(!AppendMaterialBindU64Constant(includePath, layoutHashSymbol, layout.layoutHash, inOutSymbols, scratchArena, inOutSource))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        blockCountSymbol,
        static_cast<u32>(layout.typedLayoutBlocks.size()),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        fieldCountSymbol,
        static_cast<u32>(layout.typedLayoutFields.size()),
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

    if(entry.instances.size() != layout.typedLayoutBlocks.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout block count mismatch"), StringConvert(includePath));
        return false;
    }

    for(const MaterialBindInstance& instance : entry.instances){
        MaterialBindTypedLayoutBlockLookupEntry blockEntry;
        const MaterialTypedLayoutBlock* block = nullptr;
        if(!ResolveMaterialBindGeneratedLayoutBlock(includePath, instance, layout, blockEntry, block))
            return false;

        const CookString offsetSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name) }, "_BLOCK_BYTE_OFFSET");
        const CookString sizeSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name) }, "_BLOCK_BYTE_SIZE");
        if(!AppendMaterialBindU32Constant(
            includePath,
            offsetSymbol,
            blockEntry.byteBegin,
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
        if(!AppendMaterialBindU32Constant(includePath, sizeSymbol, block->byteSize, inOutSymbols, scratchArena, inOutSource))
            return false;
    }

    return true;
}

static bool AppendMaterialBindFieldConstants(
    const AStringView includePath,
    const MaterialBindStruct& bindStruct,
    const MaterialBindInstance& instance,
    const MaterialBindField& field,
    const CookString& keySymbol,
    const CookString& defaultSymbol,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
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

    ACompactString keyText;
    if(!BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(field.name), keyText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' exceeds ACompactString capacity")
            , StringConvert(includePath)
            , StringConvert(bindStruct.name)
            , StringConvert(field.name)
        );
        return false;
    }
    const u64 keyHash = ComputeMaterialBindParameterKeyHash(keyText.view());

    if(!AppendMaterialBindU64Constant(
        includePath,
        keySymbol,
        keyHash,
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;

    if(!AppendMaterialBindConstantPrefix(
        includePath,
        defaultSymbol,
        AStringView(field.type),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;

    inOutSource += defaultAttribute;
    AppendMaterialBindConstantSuffix(inOutSource);
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
    CookArena& arena,
    const AStringView includePath,
    const MaterialBindInstance& instance,
    const MaterialBindStruct& bindStruct,
    const MaterialBindTypedLayout& layout,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    inOutSource += "\n";
    AppendMaterialBindGeneratedSeparator(inOutSource, 3u);

    MaterialBindTypedLayoutBlockLookupEntry layoutBlockEntry;
    const MaterialTypedLayoutBlock* layoutBlock = nullptr;
    if(!ResolveMaterialBindGeneratedLayoutBlock(
        includePath,
        instance,
        layout,
        layoutBlockEntry,
        layoutBlock
    ))
        return false;

    if(layoutBlock->fieldCount != bindStruct.fields.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' typed layout field count mismatch")
            , StringConvert(includePath)
            , StringConvert(instance.name)
        );
        return false;
    }

    for(u32 fieldOffset = 0u; fieldOffset < layoutBlock->fieldCount; ++fieldOffset){
        const usize layoutFieldIndex = static_cast<usize>(layoutBlock->fieldBegin) + fieldOffset;
        if(layoutFieldIndex >= layout.typedLayoutFields.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' typed layout field range exceeds layout")
                , StringConvert(includePath)
                , StringConvert(instance.name)
            );
            return false;
        }

        const MaterialBindField& field = bindStruct.fields[fieldOffset];
        const MaterialTypedLayoutField& layoutField = layout.typedLayoutFields[layoutFieldIndex];
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

        const CookString keySymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name), AStringView(field.name) }, "_KEY");
        const CookString defaultSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name), AStringView(field.name) }, "_DEFAULT");
        const CookString byteOffsetSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name), AStringView(field.name) }, "_BYTE_OFFSET");
        const CookString functionName =
            BuildMaterialBindAccessorName(arena, { AStringView(instance.name), AStringView(field.name) });
        if(!RegisterGeneratedMaterialBindSymbol(
            includePath,
            AStringView(functionName),
            inOutSymbols,
            scratchArena
        ))
            return false;

        if(!AppendMaterialBindFieldConstants(
            includePath,
            bindStruct,
            instance,
            field,
            keySymbol,
            defaultSymbol,
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
        const u32 fieldByteOffset = layoutBlockEntry.byteBegin + layoutField.offset;
        if(!AppendMaterialBindU32Constant(
            includePath,
            byteOffsetSymbol,
            fieldByteOffset,
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
        inOutSource += '\n';
        AppendMaterialBindFieldAccessor(field, byteOffsetSymbol, functionName, loadFunctionName, inOutSource);
        inOutSource += '\n';
    }

    const CookString blockFunctionName = BuildMaterialBindAccessorName(arena, { AStringView(instance.name) });
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
        const CookString functionName =
            BuildMaterialBindAccessorName(arena, { AStringView(instance.name), AStringView(field.name) });
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
    CookArena& arena,
    const MaterialBindEntry& entry,
    CookString& outSource,
    ScratchArena& scratchArena
){
    outSource.clear();

    CookString includePath(arena);
    if(!BuildMaterialBindIncludeVirtualPathImpl(arena, entry, includePath))
        return false;

    const CookString includeGuard = BuildMaterialBindIncludeGuard(arena, AStringView(includePath));

    MaterialBindTypedLayout layout(arena);
    if(!BuildMaterialBindTypedLayout(
        entry,
        Name(AStringView(entry.virtualPath)),
        layout,
        scratchArena
    ))
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
        layout,
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
            layout,
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
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialBindEntry>& materialBindEntries,
    Path& outIncludeRoot,
    ScratchArena& scratchArena
){
    outIncludeRoot.clear();
    if(materialBindEntries.empty())
        return true;

    outIncludeRoot = BuildMaterialBindIncludeRoot(cacheDirectory, configurationSafeName, scratchArena);
    if(!PrepareMaterialBindIncludeRoot(outIncludeRoot))
        return false;

    CookHashSet<CookString> seenIncludePaths{arena};
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
        if(!BuildMaterialBindIncludeSourceImpl(arena, bindEntry, generatedSource, scratchArena))
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
    const CookVector<MaterialBindEntry>& materialBindEntries,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
){
    MaterialBindInterfaceLookup materialBindLookup(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    BuildMaterialBindInterfaceLookup(materialBindEntries, materialBindLookup);

    const usize cacheReserveCount = Min(materialBindEntries.size(), materialEntries.size());
    MaterialBindTypedLayoutCache layoutCache(materialEntries.get_allocator().arena());
    layoutCache.reserve(cacheReserveCount);

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

        const MaterialBindTypedLayout* layout = nullptr;
        if(!FindOrBuildMaterialBindTypedLayout(
            materialEntry.materialInterface,
            *bindEntry,
            layoutCache,
            layout,
            scratchArena
        ))
            return false;
        if(!layout){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' failed to resolve typed layout cache for interface '{}'")
                , StringConvert(materialEntry.virtualPath.c_str())
                , StringConvert(materialEntry.materialInterface.c_str())
            );
            return false;
        }

        CopyMaterialBindTypedLayoutDefaults(
            *layout,
            materialEntry.typedLayoutHash,
            materialEntry.typedLayoutBlocks,
            materialEntry.typedLayoutFields,
            materialEntry.typedBlockBytes
        );
        if(!ApplyMaterialBindTypedLayoutParameters(
            *layout,
            materialEntry.virtualPath,
            materialEntry.parameters,
            materialEntry.typedBlockBytes
        ))
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
    MaterialCookEntry& outEntry,
    ScratchArena& scratchArena
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
        outEntry.virtualPath,
        scratchArena
    ))
        return false;
    if(!Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        "Material meta",
        MaterialAssetMetadataSchema::IsAllowedAssetField
    ))
        return false;

    if(!ParseVariantField(
        shaderCook,
        nwbFilePath,
        asset,
        MaterialAssetMetadataSchema::s_ShaderVariantField,
        outEntry.shaderVariant,
        scratchArena
    ))
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
    MaterialCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_material_asset::ParseMaterialMeta(
        shaderCook,
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        outEntry,
        scratchArena
    );
}

bool ValidateMaterialCookInterfaces(
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_material_asset::ValidateMaterialCookInterfaces(materialBindEntries, materialEntries, scratchArena);
}

bool BuildMaterialBindIncludeSource(
    MaterialCookArena& arena,
    const MaterialBindEntry& entry,
    MaterialCookString& outSource,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_material_asset::BuildMaterialBindIncludeSourceImpl(arena, entry, outSource, scratchArena);
}

bool EmitMaterialBindIncludes(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_material_asset::EmitMaterialBindIncludes(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialBindEntries,
        outIncludeRoot,
        scratchArena
    );
}

bool ResolveMaterialBindDependencyInterface(
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const MaterialCookVector<Path>& dependencies,
    MaterialCookString& outInterfacePath,
    Name& outInterfaceName,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_material_asset::ResolveMaterialBindDependencyInterface(
        shaderName,
        materialBindIncludeRoot,
        dependencies,
        outInterfacePath,
        outInterfaceName,
        scratchArena
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
    outMaterial.setTransparent(materialEntry.transparent);
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
    usize reserveBytes = sizeof(u32); // magic
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
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    AppendPOD(outBinary, MaterialBinaryPayload::s_MaterialMagic);
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

    const u32 materialFlags = material.transparent() ? MaterialBinaryPayload::s_MaterialFlagTransparent : 0u;
    AppendPOD(outBinary, materialFlags);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

