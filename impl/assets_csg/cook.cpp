// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/assets/cook_paths.h>
#include <core/assets/paths.h>
#include <core/common/log.h>

#include <global/allocation_size.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_csg_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Metascript = Core::Metascript;
using AssetsCsgCook::CsgShapeCookEntry;
using AssetsCsgCook::CookArena;
using AssetsCsgCook::CookString;
using Core::Assets::ScratchArena;
using Core::Assets::ScratchString;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<AStringView> ParseStringField(const Path& nwbFilePath, const Metascript::Value& asset, const AStringView fieldName, const b8 required){
    auto textResult = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_CsgShapeMetaDiagnosticPrefix, fieldName, required);
    if(!textResult)
        return MakeUnexpected(Failure{});
    if(!textResult->present)
        return AStringView{};
    if(TrimView(textResult->text).empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must not be empty"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }
    return textResult->text;
}

[[nodiscard]] static b8 ValidateIncludePath(const Path& nwbFilePath, const AStringView fieldName, const AStringView includePath, ScratchArena& scratchArena){
    if(includePath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must not be empty"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return false;
    }
    if(includePath.find('\\') != AStringView::npos){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must use '/' path separators"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return false;
    }

    const ::Path<ScratchArena> includePathValue(scratchArena, includePath);
    if(includePathValue.isAbsolute() || includePathValue.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must be a relative include path"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return false;
    }
    if(!IsRelativeSlashPathWithoutDotSegments(includePath)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' has invalid include path '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName), ConvertString(includePath));
        return false;
    }
    return true;
}

[[nodiscard]] static b8 HasSlangIncludeExtension(const AStringView includePath, ScratchArena& scratchArena){
    const ::Path<ScratchArena> includePathValue(scratchArena, includePath);
    static constexpr Array<AStringView, 1u> s_SlangIncludeExtensions = { s_CsgSlangIncludeExtension };
    return PathHasListedExtension(includePathValue, s_SlangIncludeExtensions);
}

// `eval` is hand-written source and `module_include` is the generated module include; both are engine/project-rooted virtual paths.
[[nodiscard]] static b8 ValidateReservedVirtualRoot(const Path& nwbFilePath, const AStringView fieldName, const AStringView includePath, ScratchArena& scratchArena){
    if(!Core::Assets::HasReservedAssetVirtualRoot(includePath, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must be a project/- or engine/-rooted virtual path " "(e.g. 'engine/csg/box/eval.slangi')"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return false;
    }
    return true;
}

[[nodiscard]] static b8 WriteModuleInclude(const Span<const CsgShapeCookEntry> csgShapeEntries, const Name shaderModule, const AStringView moduleInclude, const Path& includeRoot, ScratchArena& scratchArena, CookString& source){
    BeginCsgShapeModuleIncludeSource(source);

    for(const CsgShapeCookEntry& entry : csgShapeEntries){
        if(entry.shaderModule != shaderModule)
            continue;
        AppendCsgShapeModuleEvalInclude(entry.shapeTypeId, AStringView(entry.evalInclude), source);
    }

    const ::Path<ScratchArena> outputPath = ::Path<ScratchArena>(scratchArena, includeRoot) / moduleInclude;
    auto ensureDirectoriesResult = EnsureDirectories(outputPath.parentPath());
    if(!ensureDirectoriesResult){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to create generated include parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult.error().message()));
        return false;
    }
    if(!WriteTextFile(outputPath, AStringView(source))){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to write generated include '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}

[[nodiscard]] static b8 WriteEmptyDefaultModuleInclude(const Path& includeRoot, ScratchArena& scratchArena){
    const ::Path<ScratchArena> outputPath = ::Path<ScratchArena>(scratchArena, includeRoot) / s_DefaultGeneratedIncludeDirectory / s_CsgBuiltInModuleIncludeFileName;

    auto ensureDirectoriesResult2 = EnsureDirectories(outputPath.parentPath());
    if(!ensureDirectoriesResult2){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to create generated include parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult2.error().message()));
        return false;
    }

    if(!WriteTextFile(outputPath, s_CsgEmptyGeneratedModule)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to write generated include '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsCsgCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CsgShapeCookEntry> ParseCsgShapeCookMetadata(const Path& nwbFilePath, const Core::Metascript::Document& doc, CookArena& cookArena, ScratchArena& scratchArena){
    using namespace __hidden_assets_csg_cook;

    CsgShapeCookEntry entry(cookArena);

    const auto assetValue = Core::Assets::FindMetadataAssetMapValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, s_CsgShapeMetaDiagnosticPrefix);
    if(!assetValue)
        return MakeUnexpected(assetValue.error());
    const Core::Metascript::Value& asset = **assetValue;
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_CsgShapeMetaDiagnosticPrefix, &IsAllowedCsgShapeAssetField))
        return MakeUnexpected(Failure{});

    auto shapeNameResult = Core::Assets::ReadMetadataNameField(nwbFilePath, asset, s_CsgShapeMetaDiagnosticPrefix, s_CsgShapeField, true);
    if(!shapeNameResult)
        return MakeUnexpected(Failure{});
    entry.shapeName = *shapeNameResult;
    auto shaderModuleResult = Core::Assets::ReadMetadataNameField(nwbFilePath, asset, s_CsgShapeMetaDiagnosticPrefix, s_CsgModuleField, true);
    if(!shaderModuleResult)
        return MakeUnexpected(Failure{});
    entry.shaderModule = *shaderModuleResult;
    const auto evalInclude = ParseStringField(nwbFilePath, asset, s_CsgEvalField, true);
    if(!evalInclude)
        return MakeUnexpected(Failure{});
    entry.evalInclude = *evalInclude;
    const auto moduleInclude = ParseStringField(nwbFilePath, asset, s_CsgModuleIncludeField, false);
    if(!moduleInclude)
        return MakeUnexpected(Failure{});
    entry.moduleInclude = *moduleInclude;
    if(entry.moduleInclude.empty()){
        const AString<ScratchArena> safeModuleName = BuildCanonicalSafeCacheName(scratchArena, entry.shaderModule.resolvedText());
        auto generatedInclude = TryBuildDefaultCsgShapeModuleIncludePath(entry.shaderModule, AStringView(safeModuleName));
        if(!generatedInclude){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': failed to build generated module include for '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(entry.shaderModule.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        entry.moduleInclude = Move(*generatedInclude);
    }
    if(!ValidateIncludePath(nwbFilePath, s_CsgEvalField, AStringView(entry.evalInclude), scratchArena))
        return MakeUnexpected(Failure{});
    if(!ValidateIncludePath(nwbFilePath, s_CsgModuleIncludeField, AStringView(entry.moduleInclude), scratchArena))
        return MakeUnexpected(Failure{});
    if(!HasSlangIncludeExtension(AStringView(entry.evalInclude), scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must reference a .slangi file"), PathToString<tchar>(nwbFilePath), ConvertString(s_CsgEvalField));
        return MakeUnexpected(Failure{});
    }
    if(!HasSlangIncludeExtension(AStringView(entry.moduleInclude), scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape meta '{}': field '{}' must reference a .slangi file"), PathToString<tchar>(nwbFilePath), ConvertString(s_CsgModuleIncludeField));
        return MakeUnexpected(Failure{});
    }
    if(!ValidateReservedVirtualRoot(nwbFilePath, s_CsgEvalField, AStringView(entry.evalInclude), scratchArena))
        return MakeUnexpected(Failure{});
    if(!ValidateReservedVirtualRoot(nwbFilePath, s_CsgModuleIncludeField, AStringView(entry.moduleInclude), scratchArena))
        return MakeUnexpected(Failure{});
    return entry;
}

b8 AssignCsgShapeCookIds(const Span<CsgShapeCookEntry> csgShapeEntries, ScratchArena&){
    using namespace __hidden_assets_csg_cook;

    Sort(csgShapeEntries.begin(), csgShapeEntries.end(), [](const CsgShapeCookEntry& lhs, const CsgShapeCookEntry& rhs){
        return IsBeforeCsgShapeCookName(lhs.shapeName, rhs.shapeName);
    });

    CookArena shapeIdArena(Name("AssetsCsgCook::AssignCsgShapeCookIds"));
    HashMap<CsgShapeTypeId, Name> shapeIdNames(shapeIdArena);
    shapeIdNames.reserve(csgShapeEntries.size());

    for(usize index = 0u; index < csgShapeEntries.size(); ++index){
        CsgShapeCookEntry& entry = csgShapeEntries[index];
        if(!ResolveAndCheckCsgShapeTypeIdCollision(index, entry.shapeName, shapeIdNames, entry.shapeTypeId))
            return false;
    }

    return true;
}

Expected<Path> EmitCsgShapeModuleIncludes(const Path& cacheDirectory, const AStringView configurationSafeName, const Span<const CsgShapeCookEntry> csgShapeEntries, ScratchArena& scratchArena){
    using namespace __hidden_assets_csg_cook;

    Path includeRoot = cacheDirectory / configurationSafeName / s_GeneratedIncludeCacheDirectory;
    if(csgShapeEntries.empty()){
        if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, s_CsgShapeIncludeGenerationLabel))
            return MakeUnexpected(Failure{});
        if(!WriteEmptyDefaultModuleInclude(includeRoot, scratchArena))
            return MakeUnexpected(Failure{});
        return includeRoot;
    }

    HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>> seenShapeNames(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    seenShapeNames.reserve(csgShapeEntries.size());
    for(const CsgShapeCookEntry& entry : csgShapeEntries){
        if(!seenShapeNames.insert(entry.shapeName.hash()).second){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: duplicate shape '{}'"), ConvertString(entry.shapeName.resolvedText()));
            return MakeUnexpected(Failure{});
        }
    }

    HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>> validatedModules(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    validatedModules.reserve(csgShapeEntries.size());

    using GeneratedIncludeOwnerMap = HashMap<ScratchString, Name, ScratchArena, Hasher<ScratchString>, EqualTo<ScratchString>>;
    GeneratedIncludeOwnerMap generatedIncludeOwners(0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena);
    generatedIncludeOwners.reserve(csgShapeEntries.size());

    usize moduleSourceCapacity = 0u;

    // Preflight every output path before clearing or writing the generated include tree.
    // A later module must never be able to overwrite a generated include emitted for an earlier, unrelated module.
    for(const CsgShapeCookEntry& moduleEntry : csgShapeEntries){
        if(!validatedModules.insert(moduleEntry.shaderModule.hash()).second)
            continue;

        usize sourceBytes = s_CsgModuleSourceHeader.size();
        for(const CsgShapeCookEntry& entry : csgShapeEntries){
            if(entry.shaderModule != moduleEntry.shaderModule)
                continue;

            sourceBytes = AddSize(sourceBytes, AddSize(s_FixedShapeSourceBytes, entry.evalInclude.size()));
            if(entry.moduleInclude == moduleEntry.moduleInclude)
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: module '{}' uses multiple generated include paths ('{}' and '{}')")
                , ConvertString(moduleEntry.shaderModule.resolvedText())
                , ConvertString(moduleEntry.moduleInclude)
                , ConvertString(entry.moduleInclude)
            );
            return MakeUnexpected(Failure{});
        }

        moduleSourceCapacity = Max(moduleSourceCapacity, sourceBytes);

        ScratchString canonicalModuleInclude(moduleEntry.moduleInclude, scratchArena);
        CanonicalizeTextInPlace(canonicalModuleInclude);
        const auto generatedIncludeOwner = generatedIncludeOwners.try_emplace(Move(canonicalModuleInclude), moduleEntry.shaderModule);
        if(!generatedIncludeOwner.second && generatedIncludeOwner.first.value() != moduleEntry.shaderModule){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: generated include '{}' is shared by modules '{}' and '{}'")
                , ConvertString(moduleEntry.moduleInclude)
                , ConvertString(generatedIncludeOwner.first.value().resolvedText())
                , ConvertString(moduleEntry.shaderModule.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }
    }

    if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, s_CsgShapeIncludeGenerationLabel))
        return MakeUnexpected(Failure{});

    HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>> emittedModules(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    emittedModules.reserve(csgShapeEntries.size());

    CookString source(csgShapeEntries[0u].evalInclude.get_allocator().arena());
    source.reserve(moduleSourceCapacity);
    for(const CsgShapeCookEntry& moduleEntry : csgShapeEntries){
        if(!emittedModules.insert(moduleEntry.shaderModule.hash()).second)
            continue;

        if(!WriteModuleInclude(csgShapeEntries, moduleEntry.shaderModule, moduleEntry.moduleInclude, includeRoot, scratchArena, source))
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

