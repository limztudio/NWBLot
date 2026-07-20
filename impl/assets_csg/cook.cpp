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
using AssetsCsgCook::CookArena;
using AssetsCsgCook::CookString;
using ScratchArena = Core::Alloc::ScratchArena;
using ScratchString = AString<ScratchArena>;

static constexpr AStringView s_ShapeField = "shape";
static constexpr AStringView s_ModuleField = "module";
static constexpr AStringView s_ModuleIncludeField = "module_include";
static constexpr AStringView s_EvalField = "eval";
static constexpr AStringView s_SlangIncludeExtension = ".slangi";
static constexpr AStringView s_EvalShapeIdDefineName = "NWB_CSG_EVAL_SHAPE_ID";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool IsAllowedCsgShapeAssetField(const AStringView fieldName){
    return fieldName == s_ShapeField
        || fieldName == s_ModuleField
        || fieldName == s_ModuleIncludeField
        || fieldName == s_EvalField
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

[[nodiscard]] static bool ValidateIncludePath(
    const Path& nwbFilePath,
    const AStringView fieldName,
    const AStringView includePath,
    ScratchArena& scratchArena
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

    const ::Path<ScratchArena> includePathValue(scratchArena, includePath);
    if(includePathValue.is_absolute() || includePathValue.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must be a relative include path")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    for(const auto& component : includePathValue){
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

[[nodiscard]] static bool HasSlangIncludeExtension(const AStringView includePath, ScratchArena& scratchArena){
    const ::Path<ScratchArena> includePathValue(scratchArena, includePath);
    ScratchString extension = PathToString(scratchArena, includePathValue.extension());
    CanonicalizeTextInPlace(extension);
    return extension == s_SlangIncludeExtension;
}

// Both `eval` and `module_include` are `engine/`/`project/`-rooted virtual paths (matching `shape`/`module` and
// the material asset's `interface`/`surface`/`bxdf`). The `eval` virtual path names a hand-written source resolved
// to its absolute file in the cross-asset phase (mirroring a material's `.surface`); the `module_include` virtual
// path is the generated module written under the CSG shape include root at this virtual-prefixed sub-path (the
// dispatch shader #includes it -- mirroring a material's generated `.bind`).
[[nodiscard]] static bool ValidateReservedVirtualRoot(
    const Path& nwbFilePath,
    const AStringView fieldName,
    const AStringView includePath,
    ScratchArena& scratchArena
){
    if(!Core::Assets::HasReservedAssetVirtualRoot(includePath, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must be a project/- or engine/-rooted virtual path "
            "(e.g. 'engine/csg/box/eval.slangi')")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
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

[[nodiscard]] static bool ShapeNameLess(const CsgShapeCookEntry& lhs, const CsgShapeCookEntry& rhs){
    return AStringView(lhs.shapeName.c_str()) < AStringView(rhs.shapeName.c_str());
}

[[nodiscard]] static bool MakeShapeIdDefineValue(
    const u32 shapeTypeId,
    ScratchString& outValue
){
    char shapeTypeIdText[32] = {};
    const AStringView shapeTypeIdView = FormatDecimal(shapeTypeId, shapeTypeIdText);
    if(shapeTypeIdView.empty())
        return false;

    outValue.assign(shapeTypeIdView);
    outValue += 'u';
    return true;
}

[[nodiscard]] static bool WriteModuleInclude(
    const CsgShapeCookEntryVector& csgShapeEntries,
    const Name shaderModule,
    const CookString& moduleInclude,
    const Path& includeRoot,
    ScratchString& scratchSource
){
    scratchSource.clear();
    scratchSource += "// Generated by AssetVolumeCooker from csg_shape assets.\n";
    scratchSource += "\n";

    for(const CsgShapeCookEntry& entry : csgShapeEntries){
        if(!SameModule(entry, shaderModule))
            continue;

        ScratchString shapeTypeIdValue(scratchSource.get_allocator().arena());
        if(!MakeShapeIdDefineValue(entry.shapeTypeId, shapeTypeIdValue))
            return false;

        scratchSource += "#define ";
        scratchSource += s_EvalShapeIdDefineName;
        scratchSource += ' ';
        scratchSource += shapeTypeIdValue;
        scratchSource += "\n";
        scratchSource += "#include \"";
        scratchSource += entry.evalInclude;
        scratchSource += "\"\n";
        scratchSource += "#undef ";
        scratchSource += s_EvalShapeIdDefineName;
        scratchSource += "\n\n";
    }

    const Path outputPath = includeRoot / moduleInclude.c_str();
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

[[nodiscard]] static bool WriteEmptyDefaultModuleInclude(const Path& includeRoot){
    const Path outputPath = includeRoot / "engine" / "csg" / "generated" / "built_in.slangi";

    ErrorCode errorCode;
    if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to create generated include parent '{}': {}")
            , PathToString<tchar>(outputPath.parent_path())
            , StringConvert(errorCode.message())
        );
        return false;
    }

    static constexpr AStringView s_EmptyGeneratedModule =
        "// Generated by AssetVolumeCooker; no CSG shape assets were discovered.\n"
    ;
    if(!WriteTextFile(outputPath, s_EmptyGeneratedModule)){
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


[[nodiscard]] static bool BuildDefaultCsgShapeModuleInclude(
    CookArena& arena,
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
    CookArena& cookArena,
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
    if(!ValidateIncludePath(nwbFilePath, s_EvalField, AStringView(outEntry.evalInclude), scratchArena))
        return false;
    if(!ValidateIncludePath(nwbFilePath, s_ModuleIncludeField, AStringView(outEntry.moduleInclude), scratchArena))
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
    if(!ValidateReservedVirtualRoot(nwbFilePath, s_EvalField, AStringView(outEntry.evalInclude), scratchArena))
        return false;
    if(!ValidateReservedVirtualRoot(nwbFilePath, s_ModuleIncludeField, AStringView(outEntry.moduleInclude), scratchArena))
        return false;
    return true;
}

bool AssignCsgShapeCookIds(CsgShapeCookEntryVector& csgShapeEntries){
    using namespace __hidden_assets_csg_cook;

    Sort(csgShapeEntries.begin(), csgShapeEntries.end(), &ShapeNameLess);

    for(usize index = 0u; index < csgShapeEntries.size(); ++index){
        if(index >= static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape cook: too many CSG shape assets"));
            return false;
        }

        CsgShapeCookEntry& entry = csgShapeEntries[index];
        entry.shapeTypeId = static_cast<u32>(index) + 1u;
    }

    return true;
}

bool EmitCsgShapeModuleIncludes(
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
        return WriteEmptyDefaultModuleInclude(outIncludeRoot);

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

    ScratchString scratchSource(scratchArena);
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

