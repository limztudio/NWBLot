// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <impl/assets_csg/shape_module_source.adhd.h>

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
using Core::Assets::ScratchArena;


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
        auto generatedInclude = CsgShapeModuleSource().buildDefaultPath(entry.shaderModule, AStringView(safeModuleName));
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




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

