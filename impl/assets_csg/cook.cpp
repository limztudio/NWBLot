// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <impl/assets/csg/shape_id.h>

#include <core/assets/cook_paths.h>
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
static constexpr AStringView s_GeneratedIncludePathSeparator = "/";
static constexpr AStringView s_EvalShapeIdDefineName = "NWB_CSG_EVAL_SHAPE_ID";
static constexpr AStringView s_CsgShapeMetaDiagnosticPrefix = "CSG shape meta";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool IsAllowedCsgShapeAssetField(const AStringView fieldName)noexcept{
    return fieldName == s_ShapeField
        || fieldName == s_ModuleField
        || fieldName == s_ModuleIncludeField
        || fieldName == s_EvalField
    ;
}

[[nodiscard]] static Expected<AStringView> ParseOptionalStringField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName
){
    AStringView text;
    bool present = false;
    auto textResult = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_CsgShapeMetaDiagnosticPrefix, fieldName, false);
    if(!textResult)
        return MakeUnexpected(Failure{});
    text = textResult->text;
    present = textResult->present;
    if(!present)
        return AStringView{};
    if(TrimView(text).empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }
    return text;
}

[[nodiscard]] static Expected<AStringView> ParseRequiredStringField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName
){
    AStringView text;
    auto textResultValue = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_CsgShapeMetaDiagnosticPrefix, fieldName, true);
    if(!textResultValue)
        return MakeUnexpected(Failure{});
    text = textResultValue->text;
    if(TrimView(text).empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }
    return text;
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
    if(includePathValue.isAbsolute() || includePathValue.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must be a relative include path")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    for(const auto& component : includePathValue){
        ScratchString componentText = PathToString(scratchArena, component);
        CanonicalizeTextInPlace(componentText);
        if(componentText.empty() || GlobalFilesystemPathDetail::IsDot(AStringView(componentText.data(), componentText.size())) || GlobalFilesystemPathDetail::IsDotDot(AStringView(componentText.data(), componentText.size()))){
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

// `eval` = hand-written source (cross-asset resolved, like `.surface`); `module_include` = generated module include
// (like generated `.bind`). Both are engine//project-rooted virtual paths.
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

[[nodiscard]] static Path BuildCsgShapeIncludeRoot(const Path& cacheDirectory, const AStringView configurationSafeName){
    return cacheDirectory / configurationSafeName / "csg_modules";
}

[[nodiscard]] static bool SameModule(const CsgShapeCookEntry& entry, const Name shaderModule)noexcept{
    return entry.shaderModule == shaderModule;
}

[[nodiscard]] static bool ShapeNameLess(const CsgShapeCookEntry& lhs, const CsgShapeCookEntry& rhs){
    return lhs.shapeName.resolvedText() < rhs.shapeName.resolvedText();
}

[[nodiscard]] static Expected<ScratchString> MakeShapeIdDefineValue(
    const u32 shapeTypeId,
    ScratchArena& arena
){
    char shapeTypeIdText[TextDetail::s_DecimalTextBufferBytes] = {};
    const AStringView shapeTypeIdView = FormatDecimal(shapeTypeId, shapeTypeIdText);
    if(shapeTypeIdView.empty())
        return MakeUnexpected(Failure{});

    ScratchString value(arena);
    value.assign(shapeTypeIdView);
    value += 'u';
    return value;
}

[[nodiscard]] static bool WriteModuleInclude(
    const CsgShapeCookEntryVector& csgShapeEntries,
    const Name shaderModule,
    const AStringView moduleInclude,
    const Path& includeRoot,
    ScratchString& scratchSource
){
    scratchSource.clear();
    scratchSource += "// Generated by AssetBuilder from csg_shape assets.\n";
    scratchSource += "\n";

    for(const CsgShapeCookEntry& entry : csgShapeEntries){
        if(!SameModule(entry, shaderModule))
            continue;

        const auto shapeTypeIdValue = MakeShapeIdDefineValue(entry.shapeTypeId, scratchSource.get_allocator().arena());
        if(!shapeTypeIdValue)
            return false;

        scratchSource += "#define ";
        scratchSource += s_EvalShapeIdDefineName;
        scratchSource += ' ';
        scratchSource += *shapeTypeIdValue;
        scratchSource += "\n";
        scratchSource += "#include \"";
        scratchSource += entry.evalInclude;
        scratchSource += "\"\n";
        scratchSource += "#undef ";
        scratchSource += s_EvalShapeIdDefineName;
        scratchSource += "\n\n";
    }

    const Path outputPath = includeRoot / moduleInclude;
    ErrorCode errorCode;
    auto ensureDirectoriesResult = EnsureDirectories(outputPath.parentPath());
    if(!ensureDirectoriesResult){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to create generated include parent '{}': {}")
            , PathToString<tchar>(outputPath.parentPath())
            , StringConvert(ensureDirectoriesResult.error().message())
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
    auto ensureDirectoriesResult2 = EnsureDirectories(outputPath.parentPath());
    if(!ensureDirectoriesResult2){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to create generated include parent '{}': {}")
            , PathToString<tchar>(outputPath.parentPath())
            , StringConvert(ensureDirectoriesResult2.error().message())
        );
        return false;
    }

    static constexpr AStringView s_EmptyGeneratedModule =
        "// Generated by AssetBuilder; no CSG shape assets were discovered.\n"
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


[[nodiscard]] static Expected<CookString> BuildDefaultCsgShapeModuleInclude(
    CookArena& arena,
    const Name shaderModule
){
    CookString include(arena);
    if(!shaderModule)
        return MakeUnexpected(Failure{});

    CookString safeModuleName = BuildCanonicalSafeCacheName(arena, shaderModule.resolvedText());
    if(safeModuleName.empty())
        return MakeUnexpected(Failure{});

    include.reserve(
        s_DefaultGeneratedIncludeDirectory.size()
        + __hidden_assets_csg_cook::s_GeneratedIncludePathSeparator.size()
        + safeModuleName.size()
        + __hidden_assets_csg_cook::s_SlangIncludeExtension.size()
    );
    include += s_DefaultGeneratedIncludeDirectory;
    include += __hidden_assets_csg_cook::s_GeneratedIncludePathSeparator;
    include += safeModuleName;
    include += __hidden_assets_csg_cook::s_SlangIncludeExtension;
    return include;
}

Expected<CsgShapeCookEntry> ParseCsgShapeCookMetadata(
    CookArena& cookArena,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    ScratchArena& scratchArena
){
    using namespace __hidden_assets_csg_cook;

    CsgShapeCookEntry entry(cookArena);

    const Core::Metascript::Value& asset = doc.asset();
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, "CSG shape meta"))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        "CSG shape meta",
        &IsAllowedCsgShapeAssetField
    ))
        return MakeUnexpected(Failure{});

    auto shapeNameResult = Core::Assets::ReadMetadataNameField(nwbFilePath, asset, s_CsgShapeMetaDiagnosticPrefix, s_ShapeField, true);
    if(!shapeNameResult)
        return MakeUnexpected(Failure{});
    entry.shapeName = *shapeNameResult;
    auto shaderModuleResult = Core::Assets::ReadMetadataNameField(nwbFilePath, asset, s_CsgShapeMetaDiagnosticPrefix, s_ModuleField, true);
    if(!shaderModuleResult)
        return MakeUnexpected(Failure{});
    entry.shaderModule = *shaderModuleResult;
    const auto evalInclude = ParseRequiredStringField(nwbFilePath, asset, s_EvalField);
    if(!evalInclude)
        return MakeUnexpected(Failure{});
    entry.evalInclude = *evalInclude;
    const auto moduleInclude = ParseOptionalStringField(nwbFilePath, asset, s_ModuleIncludeField);
    if(!moduleInclude)
        return MakeUnexpected(Failure{});
    entry.moduleInclude = *moduleInclude;
    if(entry.moduleInclude.empty()){
        auto generatedInclude = BuildDefaultCsgShapeModuleInclude(cookArena, entry.shaderModule);
        if(!generatedInclude){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': failed to build generated module include for '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(entry.shaderModule.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }
        entry.moduleInclude = Move(*generatedInclude);
    }
    if(!ValidateIncludePath(nwbFilePath, s_EvalField, AStringView(entry.evalInclude), scratchArena))
        return MakeUnexpected(Failure{});
    if(!ValidateIncludePath(nwbFilePath, s_ModuleIncludeField, AStringView(entry.moduleInclude), scratchArena))
        return MakeUnexpected(Failure{});
    if(!HasSlangIncludeExtension(AStringView(entry.evalInclude), scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must reference a .slangi file")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_EvalField)
        );
        return MakeUnexpected(Failure{});
    }
    if(!HasSlangIncludeExtension(AStringView(entry.moduleInclude), scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must reference a .slangi file")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_ModuleIncludeField)
        );
        return MakeUnexpected(Failure{});
    }
    if(!ValidateReservedVirtualRoot(nwbFilePath, s_EvalField, AStringView(entry.evalInclude), scratchArena))
        return MakeUnexpected(Failure{});
    if(!ValidateReservedVirtualRoot(nwbFilePath, s_ModuleIncludeField, AStringView(entry.moduleInclude), scratchArena))
        return MakeUnexpected(Failure{});
    return entry;
}

bool AssignCsgShapeCookIds(CsgShapeCookEntryVector& csgShapeEntries){
    using namespace __hidden_assets_csg_cook;

    Sort(csgShapeEntries.begin(), csgShapeEntries.end(), &ShapeNameLess);

    using ShapeIdNameMap = HashMap<CsgShapeTypeId, Name, CookArena, Hasher<CsgShapeTypeId>, EqualTo<CsgShapeTypeId>>;
    CookArena& cookArena = csgShapeEntries.get_allocator().arena();
    ShapeIdNameMap shapeIdNames(0, Hasher<CsgShapeTypeId>(), EqualTo<CsgShapeTypeId>(), cookArena);
    shapeIdNames.reserve(csgShapeEntries.size());

    for(usize index = 0u; index < csgShapeEntries.size(); ++index){
        if(index >= static_cast<usize>(Limit<CsgShapeTypeId>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape cook: too many CSG shape assets"));
            return false;
        }

        CsgShapeCookEntry& entry = csgShapeEntries[index];
        const CsgShapeTypeId shapeTypeId = CsgShapeTypeIdFromName(entry.shapeName);
        if(shapeTypeId == s_InvalidCsgShapeTypeId){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape cook: shape '{}' has an invalid canonical GPU id"), StringConvert(entry.shapeName.resolvedText()));
            return false;
        }

        const auto foundShapeId = shapeIdNames.find(shapeTypeId);
        if(foundShapeId != shapeIdNames.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape cook: shapes '{}' and '{}' collide on canonical GPU id {}")
                , StringConvert(foundShapeId.value().resolvedText())
                , StringConvert(entry.shapeName.resolvedText())
                , shapeTypeId
            );
            return false;
        }

        shapeIdNames.emplace(shapeTypeId, entry.shapeName);
        entry.shapeTypeId = shapeTypeId;
    }

    return true;
}

Expected<Path> EmitCsgShapeModuleIncludes(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CsgShapeCookEntryVector& csgShapeEntries,
    ScratchArena& scratchArena
){
    using namespace __hidden_assets_csg_cook;

    Path includeRoot = BuildCsgShapeIncludeRoot(cacheDirectory, configurationSafeName);
    if(csgShapeEntries.empty()){
        if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, "CSG shape include generation"))
            return MakeUnexpected(Failure{});
        if(!WriteEmptyDefaultModuleInclude(includeRoot))
            return MakeUnexpected(Failure{});
        return includeRoot;
    }

    HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>> seenShapeNames(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        scratchArena
    );
    seenShapeNames.reserve(csgShapeEntries.size());
    for(const CsgShapeCookEntry& entry : csgShapeEntries){
        if(!seenShapeNames.insert(entry.shapeName.hash()).second){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: duplicate shape '{}'"), StringConvert(entry.shapeName.resolvedText()));
            return MakeUnexpected(Failure{});
        }
    }

    HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>> validatedModules(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        scratchArena
    );
    validatedModules.reserve(csgShapeEntries.size());

    using GeneratedIncludeOwnerMap = HashMap<ScratchString, Name, ScratchArena, Hasher<ScratchString>, EqualTo<ScratchString>>;
    GeneratedIncludeOwnerMap generatedIncludeOwners(0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena);
    generatedIncludeOwners.reserve(csgShapeEntries.size());

    // Preflight every output path before clearing or writing the generated include tree.  A later module must never
    // be able to overwrite a generated include emitted for an earlier, unrelated module.
    for(const CsgShapeCookEntry& moduleEntry : csgShapeEntries){
        if(!validatedModules.insert(moduleEntry.shaderModule.hash()).second)
            continue;

        for(const CsgShapeCookEntry& entry : csgShapeEntries){
            if(entry.shaderModule != moduleEntry.shaderModule)
                continue;
            if(entry.moduleInclude == moduleEntry.moduleInclude)
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: module '{}' uses multiple generated include paths ('{}' and '{}')")
                , StringConvert(moduleEntry.shaderModule.resolvedText())
                , StringConvert(moduleEntry.moduleInclude)
                , StringConvert(entry.moduleInclude)
            );
            return MakeUnexpected(Failure{});
        }

        ScratchString canonicalModuleInclude(moduleEntry.moduleInclude, scratchArena);
        CanonicalizeTextInPlace(canonicalModuleInclude);
        const auto generatedIncludeOwner = generatedIncludeOwners.try_emplace(
            Move(canonicalModuleInclude),
            moduleEntry.shaderModule
        );
        if(!generatedIncludeOwner.second && generatedIncludeOwner.first.value() != moduleEntry.shaderModule){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: generated include '{}' is shared by modules '{}' and '{}'")
                , StringConvert(moduleEntry.moduleInclude)
                , StringConvert(generatedIncludeOwner.first.value().resolvedText())
                , StringConvert(moduleEntry.shaderModule.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }
    }

    if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, "CSG shape include generation"))
        return MakeUnexpected(Failure{});

    HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>> emittedModules(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        scratchArena
    );
    emittedModules.reserve(csgShapeEntries.size());

    ScratchString scratchSource(scratchArena);
    for(const CsgShapeCookEntry& moduleEntry : csgShapeEntries){
        if(!emittedModules.insert(moduleEntry.shaderModule.hash()).second)
            continue;

        if(!WriteModuleInclude(csgShapeEntries, moduleEntry.shaderModule, moduleEntry.moduleInclude, includeRoot, scratchSource))
            return MakeUnexpected(Failure{});
    }

    return includeRoot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

