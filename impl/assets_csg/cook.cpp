// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/assets/paths.h>
#include <core/common/log.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_csg_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Metascript = Core::Metascript;
using AssetsCsgCook::CsgShapeCookEntry;
using AssetsCsgCook::CsgShapeCookEntryVector;
using CookString = ShaderCook::CookString;
using ScratchArena = Core::Alloc::ScratchArena;
using ScratchString = AString<ScratchArena>;

static constexpr AStringView s_ShapeField = "shape";
static constexpr AStringView s_ModuleField = "module";
static constexpr AStringView s_ModuleIncludeField = "module_include";
static constexpr AStringView s_EvalField = "eval";
static constexpr AStringView s_ProxyShaderField = "proxy_shader";
static constexpr AStringView s_ProxySourceField = "proxy_source";
static constexpr AStringView s_ProxyEntryField = "proxy_entry";
static constexpr AStringView s_ProxyProfileField = "proxy_profile";
static constexpr AStringView s_ProxyIncludeRootsField = "proxy_include_roots";
static constexpr AStringView s_ProxyShadowField = "proxy_shadow";
static constexpr AStringView s_SlangIncludeExtension = ".slangi";
static constexpr AStringView s_SlangSourceExtension = ".slang";
static constexpr AStringView s_DefaultProxyEntryPoint = "main";
static constexpr AStringView s_DefaultProxyTargetProfile = "spirv_1_5";
static constexpr AStringView s_DefaultProxyIncludeRoot = "engine/graphics";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool IsAllowedCsgShapeAssetField(const AStringView fieldName){
    return fieldName == s_ShapeField
        || fieldName == s_ModuleField
        || fieldName == s_ModuleIncludeField
        || fieldName == s_EvalField
        || fieldName == s_ProxyShaderField
        || fieldName == s_ProxySourceField
        || fieldName == s_ProxyEntryField
        || fieldName == s_ProxyProfileField
        || fieldName == s_ProxyIncludeRootsField
        || fieldName == s_ProxyShadowField
    ;
}

[[nodiscard]] static bool ParseRequiredNameField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    Name& outName
){
    outName = NAME_NONE;

    const Metascript::Value* fieldValue = asset.findField(fieldName);
    if(!fieldValue){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' is required")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must be a string")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const Metascript::MStringView text = fieldValue->asString();
    outName = Name(AStringView(text.data(), text.size()));
    if(!outName){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

[[nodiscard]] static bool ParseOptionalNameField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    Name& outName
){
    outName = NAME_NONE;

    const Metascript::Value* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return true;
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must be a string")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const Metascript::MStringView text = fieldValue->asString();
    outName = Name(AStringView(text.data(), text.size()));
    if(!outName){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

[[nodiscard]] static bool ParseOptionalStringField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    CookString& outText
){
    outText.clear();

    const Metascript::Value* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return true;
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must be a string")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const Metascript::MStringView text = fieldValue->asString();
    outText.assign(text.data(), text.size());
    if(TrimView(AStringView(outText)).empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

[[nodiscard]] static bool ParseRequiredStringField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    CookString& outText
){
    if(!asset.findField(fieldName)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' is required")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return ParseOptionalStringField(nwbFilePath, asset, fieldName, outText);
}

[[nodiscard]] static bool ParseOptionalIntegerFlagField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    bool& inOutValue
){
    const Metascript::Value* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return true;

    if(!fieldValue->isInteger()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must be 0 or 1")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const i64 value = fieldValue->asInteger();
    if(value != 0 && value != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must be 0 or 1")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    inOutValue = value != 0;
    return true;
}

[[nodiscard]] static bool ParseOptionalStringListField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    ShaderCook::CookVector<CookString>& outList
){
    outList.clear();

    const Metascript::Value* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return true;
    if(!fieldValue->copyStringList(outList)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must be a list of strings")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    for(const CookString& text : outList){
        if(!TrimView(AStringView(text)).empty())
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' entries must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

[[nodiscard]] static bool ValidateIncludePath(
    const Path& nwbFilePath,
    const AStringView fieldName,
    const AStringView includePath
){
    if(includePath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    if(includePath.find('\\') != AStringView::npos){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must use '/' path separators")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const Path includePathValue(includePath);
    if(includePathValue.is_absolute() || includePathValue.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must be a relative include path")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    for(const Path& component : includePathValue){
        ScratchArena scratchArena;
        ScratchString componentText = PathToString(scratchArena, component);
        CanonicalizeTextInPlace(componentText);
        if(componentText.empty() || componentText == "." || componentText == ".."){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' has invalid include path '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(fieldName)
                , StringConvert(includePath)
            );
            return false;
        }
    }
    return true;
}

[[nodiscard]] static bool ResolveRelativeSourcePath(
    ShaderCook::CookArena& cookArena,
    const Path& nwbFilePath,
    const AStringView fieldName,
    const AStringView sourcePath,
    CookString& outSource
){
    outSource.clear();
    if(!ValidateIncludePath(nwbFilePath, fieldName, sourcePath))
        return false;

    const Path parentDirectory = nwbFilePath.parent_path();
    if(parentDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' cannot be resolved because the metadata directory is empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const Path resolvedSourcePath = (parentDirectory / Path(sourcePath)).lexically_normal();
    const CookString resolvedSourcePathText = PathToString(cookArena, resolvedSourcePath);
    outSource.assign(resolvedSourcePathText.data(), resolvedSourcePathText.size());
    return !outSource.empty();
}

[[nodiscard]] static bool HasSlangIncludeExtension(const AStringView includePath, ScratchArena& scratchArena){
    const Path includePathValue(includePath);
    ScratchString extension = PathToString(scratchArena, includePathValue.extension());
    CanonicalizeTextInPlace(extension);
    return extension == s_SlangIncludeExtension;
}

[[nodiscard]] static bool HasSlangSourceExtension(const AStringView sourcePath, ScratchArena& scratchArena){
    const Path sourcePathValue(sourcePath);
    ScratchString extension = PathToString(scratchArena, sourcePathValue.extension());
    CanonicalizeTextInPlace(extension);
    return extension == s_SlangSourceExtension;
}

[[nodiscard]] static bool HasProxyShaderFields(const Metascript::Value& asset){
    return asset.findField(s_ProxyShaderField)
        || asset.findField(s_ProxySourceField)
        || asset.findField(s_ProxyEntryField)
        || asset.findField(s_ProxyProfileField)
        || asset.findField(s_ProxyIncludeRootsField)
        || asset.findField(s_ProxyShadowField)
    ;
}

[[nodiscard]] static bool ParseProxyShaderEntry(
    ShaderCook::CookArena& cookArena,
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    ShaderCook::ShaderEntry& outEntry,
    ScratchArena& scratchArena
){
    outEntry = ShaderCook::ShaderEntry(cookArena);
    if(!HasProxyShaderFields(asset))
        return true;

    Name proxyShaderName = NAME_NONE;
    if(!ParseOptionalNameField(nwbFilePath, asset, s_ProxyShaderField, proxyShaderName))
        return false;
    if(!proxyShaderName){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' is required when proxy shader fields are present")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_ProxyShaderField)
        );
        return false;
    }

    CookString proxySource(cookArena);
    if(!ParseRequiredStringField(nwbFilePath, asset, s_ProxySourceField, proxySource))
        return false;
    if(!HasSlangSourceExtension(AStringView(proxySource), scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must reference a .slang file")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_ProxySourceField)
        );
        return false;
    }

    outEntry.name.assign(AStringView(proxyShaderName.c_str()));
    if(!outEntry.stage.assign("mesh"))
        return false;
    if(!outEntry.archiveStage.assign(outEntry.stage))
        return false;
    if(!outEntry.targetProfile.assign(s_DefaultProxyTargetProfile))
        return false;
    outEntry.entryPoint.assign(s_DefaultProxyEntryPoint);
    outEntry.emitMeshComputeShadow = false;
    outEntry.includeRoots.emplace_back(s_DefaultProxyIncludeRoot, cookArena);

    CookString proxyProfile(cookArena);
    if(!ParseOptionalStringField(nwbFilePath, asset, s_ProxyProfileField, proxyProfile))
        return false;
    if(!proxyProfile.empty() && !outEntry.targetProfile.assign(AStringView(proxyProfile)))
        return false;
    CookString proxyEntry(cookArena);
    if(!ParseOptionalStringField(nwbFilePath, asset, s_ProxyEntryField, proxyEntry))
        return false;
    if(!proxyEntry.empty())
        outEntry.entryPoint.assign(AStringView(proxyEntry));
    if(!ParseOptionalStringListField(nwbFilePath, asset, s_ProxyIncludeRootsField, outEntry.includeRoots))
        return false;
    if(outEntry.includeRoots.empty())
        outEntry.includeRoots.emplace_back(s_DefaultProxyIncludeRoot, cookArena);
    if(!ParseOptionalIntegerFlagField(nwbFilePath, asset, s_ProxyShadowField, outEntry.emitMeshComputeShadow))
        return false;

    if(outEntry.targetProfile.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_ProxyProfileField)
        );
        return false;
    }
    if(outEntry.entryPoint.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_ProxyEntryField)
        );
        return false;
    }

    return ResolveRelativeSourcePath(cookArena, nwbFilePath, s_ProxySourceField, AStringView(proxySource), outEntry.source);
}

[[nodiscard]] static Path BuildCsgShapeIncludeRoot(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    ScratchArena& scratchArena
){
    ScratchString configurationName(configurationSafeName, scratchArena);
    return cacheDirectory / configurationName.c_str() / "csg_modules";
}

[[nodiscard]] static bool PrepareIncludeRoot(const Path& includeRoot){
    ErrorCode errorCode;
    if(!RemoveAllIfExists(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to clear generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    errorCode.clear();
    if(!EnsureDirectories(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to create generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }
    return true;
}

[[nodiscard]] static bool SameModule(const CsgShapeCookEntry& entry, const Name shaderModule){
    return entry.shaderModule == shaderModule;
}

[[nodiscard]] static bool WriteModuleInclude(
    const CsgShapeCookEntryVector& csgShapeEntries,
    const Name shaderModule,
    const CookString& moduleInclude,
    const Path& includeRoot,
    CookString& scratchSource
){
    scratchSource.clear();
    scratchSource += "// Generated by GraphicsAssetCooker from csg_shape assets.\n";
    scratchSource += "\n";

    for(const CsgShapeCookEntry& entry : csgShapeEntries){
        if(!SameModule(entry, shaderModule))
            continue;

        scratchSource += "#include \"";
        scratchSource += entry.evalInclude;
        scratchSource += "\"\n";
    }

    const Path outputPath = includeRoot / Path(moduleInclude.c_str());
    ErrorCode errorCode;
    if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to create generated include parent '{}': {}")
            , PathToString<tchar>(outputPath.parent_path())
            , StringConvert(errorCode.message())
        );
        return false;
    }
    if(!WriteTextFile(outputPath, AStringView(scratchSource))){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to write generated include '{}'")
            , PathToString<tchar>(outputPath)
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsCsgCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildDefaultCsgShapeModuleInclude(
    ShaderCook::CookArena& arena,
    const Name shaderModule,
    CookString& outInclude
){
    outInclude.clear();
    if(!shaderModule)
        return false;

    CookString safeModuleName = BuildCanonicalSafeCacheName(arena, AStringView(shaderModule.c_str()));
    if(safeModuleName.empty())
        return false;

    outInclude.reserve(s_DefaultGeneratedIncludeDirectory.size() + safeModuleName.size() + 8u);
    outInclude += s_DefaultGeneratedIncludeDirectory;
    outInclude += '/';
    outInclude += safeModuleName;
    outInclude += ".slangi";
    return true;
}

bool ParseCsgShapeCookMetadata(
    ShaderCook::CookArena& cookArena,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    CsgShapeCookEntry& outEntry,
    ScratchArena& scratchArena
){
    using namespace __hidden_assets_csg_cook;

    outEntry = CsgShapeCookEntry(cookArena);

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': asset is not a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(!Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        "CSG shape meta",
        &IsAllowedCsgShapeAssetField
    ))
        return false;

    if(!ParseRequiredNameField(nwbFilePath, asset, s_ShapeField, outEntry.shapeName))
        return false;
    if(!ParseRequiredNameField(nwbFilePath, asset, s_ModuleField, outEntry.shaderModule))
        return false;
    if(!ParseRequiredStringField(nwbFilePath, asset, s_EvalField, outEntry.evalInclude))
        return false;
    if(!ParseOptionalStringField(nwbFilePath, asset, s_ModuleIncludeField, outEntry.moduleInclude))
        return false;
    if(outEntry.moduleInclude.empty() && !BuildDefaultCsgShapeModuleInclude(cookArena, outEntry.shaderModule, outEntry.moduleInclude)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': failed to build generated module include for '{}'")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(outEntry.shaderModule.c_str())
        );
        return false;
    }
    if(!ValidateIncludePath(nwbFilePath, s_EvalField, AStringView(outEntry.evalInclude)))
        return false;
    if(!ValidateIncludePath(nwbFilePath, s_ModuleIncludeField, AStringView(outEntry.moduleInclude)))
        return false;
    if(!HasSlangIncludeExtension(AStringView(outEntry.evalInclude), scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must reference a .slangi file")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_EvalField)
        );
        return false;
    }
    if(!HasSlangIncludeExtension(AStringView(outEntry.moduleInclude), scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must reference a .slangi file")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_ModuleIncludeField)
        );
        return false;
    }
    if(!ParseProxyShaderEntry(cookArena, nwbFilePath, asset, outEntry.proxyShaderEntry, scratchArena))
        return false;

    return true;
}

bool EmitCsgShapeModuleIncludes(
    ShaderCook::CookArena& cookArena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CsgShapeCookEntryVector& csgShapeEntries,
    Path& outIncludeRoot,
    ScratchArena& scratchArena
){
    using namespace __hidden_assets_csg_cook;

    outIncludeRoot.clear();
    outIncludeRoot = BuildCsgShapeIncludeRoot(cacheDirectory, configurationSafeName, scratchArena);
    if(!PrepareIncludeRoot(outIncludeRoot))
        return false;
    if(csgShapeEntries.empty())
        return true;

    HashSet<NameHash, Hasher<NameHash>, EqualTo<NameHash>, ScratchArena> seenShapeNames(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        scratchArena
    );
    seenShapeNames.reserve(csgShapeEntries.size());
    for(const CsgShapeCookEntry& entry : csgShapeEntries){
        if(!seenShapeNames.insert(entry.shapeName.hash()).second){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: duplicate shape '{}'"), StringConvert(entry.shapeName.c_str()));
            return false;
        }
    }

    HashSet<NameHash, Hasher<NameHash>, EqualTo<NameHash>, ScratchArena> seenModules(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        scratchArena
    );
    seenModules.reserve(csgShapeEntries.size());

    CookString scratchSource(cookArena);
    for(const CsgShapeCookEntry& moduleEntry : csgShapeEntries){
        if(!seenModules.insert(moduleEntry.shaderModule.hash()).second)
            continue;

        for(const CsgShapeCookEntry& entry : csgShapeEntries){
            if(entry.shaderModule != moduleEntry.shaderModule)
                continue;
            if(entry.moduleInclude == moduleEntry.moduleInclude)
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: module '{}' uses multiple generated include paths ('{}' and '{}')")
                , StringConvert(moduleEntry.shaderModule.c_str())
                , StringConvert(moduleEntry.moduleInclude)
                , StringConvert(entry.moduleInclude)
            );
            return false;
        }

        if(!WriteModuleInclude(csgShapeEntries, moduleEntry.shaderModule, moduleEntry.moduleInclude, outIncludeRoot, scratchSource))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
