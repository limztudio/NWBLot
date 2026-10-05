// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include "slang_compiler.h"
#include "arena_names.h"
#include "binary_payload.h"

#include <core/assets/paths.h>
#include <core/metascript/parser.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_cook{
static constexpr StringView s_NoneOptText = "none";
static constexpr StringView s_DefaultVariantText = "default";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;
namespace Alloc = Core::Alloc;
namespace Metascript = Core::Metascript;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookString = ShaderCook::CookString;
template<typename T>
using CookVector = ShaderCook::CookVector<T>;
template<typename T, typename V>
using CookMap = ShaderCook::CookMap<T, V>;
template<typename T>
using CookHashSet = ShaderCook::CookHashSet<T>;
using ScratchString = AString<Alloc::ScratchArena>;
template<typename T>
using ScratchVector = Vector<T, Alloc::ScratchArena>;
template<typename T>
using ScratchHashSet = HashSet<T, Alloc::ScratchArena, Hasher<T>, EqualTo<T>>;
static constexpr AStringView s_AssetTypeShader = "shader";
static constexpr AStringView s_AssetTypeInclude = "include";
static constexpr AStringView s_SlangSourceExtension = ".slang";
static constexpr AStringView s_SlangIncludeExtension = ".slangi";
static bool TryParseShaderOptimizationLevel(
    const AStringView text,
    ShaderOptimizationLevel::Enum& outOptimizationLevel
){
    if(text == ::__hidden_shader_cook::s_NoneOptText){
        outOptimizationLevel = ShaderOptimizationLevel::None;
        return true;
    }
    if(text == ::__hidden_shader_cook::s_DefaultVariantText){
        outOptimizationLevel = ShaderOptimizationLevel::Default;
        return true;
    }
    if(text == "high"){
        outOptimizationLevel = ShaderOptimizationLevel::High;
        return true;
    }
    if(text == "maximal"){
        outOptimizationLevel = ShaderOptimizationLevel::Maximal;
        return true;
    }

    outOptimizationLevel = ShaderOptimizationLevel::kCount;
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static ACompactString CanonicalAssetType(const Metascript::Document& doc){
    const auto assetType = doc.assetType();
    return ACompactString(AStringView(assetType.data(), assetType.size()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename VisitedSet>
static bool CollectDependencies(const Path& startPath, const ShaderCook::CookVector<Path>& includeDirectories, VisitedSet& inOutVisitedPaths, ShaderCook::CookVector<Path>& inOutDependencies, Alloc::ScratchArena& scratchArena){
    ErrorCode errorCode;

    Deque<Path, Alloc::ScratchArena> pending{scratchArena};
    pending.push_back(startPath);
    ScratchString sourceText{scratchArena};
    Path includePath(inOutDependencies.get_allocator().arena());

    while(!pending.empty()){
        Path dependencyPath = Move(pending.back());
        pending.pop_back();

        const Path absolutePath = AbsolutePath(dependencyPath, errorCode).lexicallyNormal();
        if(errorCode){
            NWB_LOGGER_ERROR(GLB_TEXT("Failed to resolve dependency path '{}' : {}")
                , PathToString<tchar>(dependencyPath)
                , StringConvert(errorCode.message())
            );
            return false;
        }

        ScratchString canonicalPathKey = PathToString(scratchArena, absolutePath);
        CanonicalizeTextInPlace(canonicalPathKey);
        if(!inOutVisitedPaths.insert(Move(canonicalPathKey)).second)
            continue;

        sourceText.clear();
        if(!ReadTextFile(absolutePath, sourceText)){
            NWB_LOGGER_ERROR(GLB_TEXT("Failed to read dependency '{}'"), PathToString<tchar>(absolutePath));
            return false;
        }
        StripUtf8Bom(sourceText);

        inOutDependencies.push_back(absolutePath);

        const AStringView sourceView(sourceText.data(), sourceText.size());
        usize lineBegin = 0;
        while(lineBegin < sourceView.size()){
            usize lineEnd = lineBegin;
            while(lineEnd < sourceView.size() && sourceView[lineEnd] != '\n')
                ++lineEnd;

            AStringView line = sourceView.substr(lineBegin, lineEnd - lineBegin);
            if(!line.empty() && line.back() == '\r')
                line.remove_suffix(1);

            AStringView includeName;
            ShaderIncludeKind::Enum includeKind = ShaderIncludeKind::Relative;
            if(SlangShaderCompiler::extractIncludeDirective(line, includeName, includeKind)){
                if(!SlangShaderCompiler::resolveIncludeFile(includeName, includeKind, absolutePath.parentPath(), includeDirectories, includePath)){
                    NWB_LOGGER_ERROR(GLB_TEXT("Unable to resolve include '{}' from '{}'")
                        , StringConvert(includeName)
                        , PathToString<tchar>(absolutePath)
                    );
                    return false;
                }

                pending.push_back(Move(includePath));
            }

            lineBegin = lineEnd + 1;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename DefineMap>
static bool ValidateVariantSignature(const AStringView contextLabel, const AStringView variantSignature, const DefineMap& defineValues, Alloc::ScratchArena& scratchArena){
    if(variantSignature.empty())
        return true;

    if(variantSignature == ::__hidden_shader_cook::s_DefaultVariantText){
        if(defineValues.empty())
            return true;

        NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': variant 'default' is only valid when no defines are specified"), StringConvert(contextLabel));
        return false;
    }

    if(defineValues.empty()){
        NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': variant '{}' requires defines to be specified")
            , StringConvert(contextLabel)
            , StringConvert(variantSignature)
        );
        return false;
    }

    using ScratchStringSet = ScratchHashSet<AStringView>;
    ScratchStringSet seenDefines{0, Hasher<AStringView>(), EqualTo<AStringView>(), scratchArena};
    seenDefines.reserve(defineValues.size());
    usize begin = 0;
    const auto logInvalidAssignment = [&](const AStringView segment){
        NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': variant '{}' has invalid assignment '{}'")
            , StringConvert(contextLabel)
            , StringConvert(variantSignature)
            , StringConvert(segment)
        );
    };
    while(begin < variantSignature.size()){
        usize segmentEnd = variantSignature.find(';', begin);
        if(segmentEnd == AStringView::npos)
            segmentEnd = variantSignature.size();

        const AStringView segment = TrimView(variantSignature.substr(begin, segmentEnd - begin));
        if(segment.empty()){
            NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': variant '{}' has invalid empty segment")
                , StringConvert(contextLabel)
                , StringConvert(variantSignature)
            );
            return false;
        }

        const usize equalPos = segment.find('=');
        if(equalPos == AStringView::npos || equalPos == 0 || equalPos + 1 >= segment.size()){
            logInvalidAssignment(segment);
            return false;
        }

        const AStringView defineName = TrimView(segment.substr(0, equalPos));
        const AStringView defineValue = TrimView(segment.substr(equalPos + 1));
        if(defineName.empty() || defineValue.empty()){
            logInvalidAssignment(segment);
            return false;
        }

        CookString lookupDefineName(defineName, defineValues.get_allocator().arena());
        const auto defineIt = defineValues.find(lookupDefineName);
        if(defineIt == defineValues.end()){
            NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': variant '{}' references unknown define '{}'")
                , StringConvert(contextLabel)
                , StringConvert(variantSignature)
                , StringConvert(defineName)
            );
            return false;
        }

        bool valueFound = false;
        for(const CookString& allowedValue : defineIt.value().values){
            if(AStringView(allowedValue) == defineValue){
                valueFound = true;
                break;
            }
        }
        if(!valueFound){
            NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': variant '{}' has unsupported value '{}' for define '{}'")
                , StringConvert(contextLabel)
                , StringConvert(variantSignature)
                , StringConvert(defineValue)
                , StringConvert(defineName)
            );
            return false;
        }

        if(!seenDefines.insert(defineName).second){
            NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': variant '{}' assigns define '{}' more than once")
                , StringConvert(contextLabel)
                , StringConvert(variantSignature)
                , StringConvert(defineName)
            );
            return false;
        }

        begin = segmentEnd + 1;
    }

    if(seenDefines.size() != defineValues.size()){
        NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': variant '{}' must assign all defines")
            , StringConvert(contextLabel)
            , StringConvert(variantSignature)
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMetascriptDocument(const Path& sourceFilePath, const AStringView sourceKind, ShaderCook::CookArena& arena, Metascript::Document& outDoc){
    CookString metaText{arena};
    return Assets::ParseMetadataDocumentText(
        sourceFilePath,
        sourceKind,
        metaText,
        outDoc,
        [&](const AStringView text){ return outDoc.parse(text); }
    );
}

static bool ValidatePairedSourceExtension(
    const Path& nwbFilePath,
    const AStringView sourcePath,
    const AStringView expectedExtension,
    const AStringView metaKind,
    Alloc::ScratchArena& scratchArena
){
    return Assets::CheckPairedSourceExtension(nwbFilePath, sourcePath, expectedExtension, metaKind, scratchArena);
}

static bool ParseOptionalIntegerFlagField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    bool& inOutValue
){
    const Metascript::Value* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return true;

    if(!fieldValue->isInteger()){
        NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': field '{}' must be 0 or 1")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const i64 value = fieldValue->asInteger();
    if(value != 0 && value != 1){
        NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': field '{}' must be 0 or 1")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    inOutValue = value != 0;
    return true;
}

static bool ParseDefines(const Path& nwbFilePath, const Metascript::Value& asset, ShaderCook::CookArena& arena, ShaderCook::CookMap<CookString, ShaderCook::DefineEntry>& outDefineValues){
    outDefineValues.clear();

    const auto* definesVal = asset.findField("defines");
    if(!definesVal)
        return true;

    if(!definesVal->isMap()){
        NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': defines must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    const auto& definesMap = definesVal->asMap();
    outDefineValues.reserve(definesMap.size());
    for(const auto& [key, val] : definesMap){
        if(AStringView(key.data(), key.size()) == "NWB_BINDLESS_TLAS"){
            NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': define 'NWB_BINDLESS_TLAS' is an engine transport feature selected in shader source")
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
        CookString defineName(key.data(), key.size(), arena);
        if(defineName.empty()){
            NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': define names must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }

        ShaderCook::CookVector<CookString> defineValues(arena);
        if(!val.copyStringList(defineValues)){
            NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': define '{}' values must be a list of strings"), PathToString<tchar>(nwbFilePath), StringConvert(defineName));
            return false;
        }
        if(defineValues.empty()){
            NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': define '{}' must provide at least one value")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(defineName)
            );
            return false;
        }
        for(const CookString& defineValue : defineValues){
            if(!defineValue.empty())
                continue;

            NWB_LOGGER_ERROR(GLB_TEXT("Meta '{}': define '{}' values must not be empty")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(defineName)
            );
            return false;
        }

        ShaderCook::DefineEntry defineEntry(Move(defineValues));
        outDefineValues.insert_or_assign(Move(defineName), Move(defineEntry));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderCook::ShaderCook(CookArena& memoryArena, ShaderCompilerFactory compilerFactory)
    : m_memoryArena(memoryArena)
{
    const ShaderCompilerFactory createCompiler = compilerFactory ? compilerFactory : CreateSlangShaderCompiler;
    m_compiler = createCompiler(m_memoryArena);
}


bool ShaderCook::parseDocument(const Path& nwbFilePath, Metascript::Document& outDoc){
    return __hidden_shader_cook::ParseMetascriptDocument(nwbFilePath, "Meta", m_memoryArena, outDoc);
}


bool ShaderCook::validateVariantSignature(
    const AStringView contextLabel,
    const AStringView variantSignature,
    const CookMap<CookString, DefineEntry>& defineValues,
    Alloc::ScratchArena& scratchArena
){
    return __hidden_shader_cook::ValidateVariantSignature(contextLabel, variantSignature, defineValues, scratchArena);
}

bool ShaderCook::parseShaderMeta(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    ShaderEntry& outEntry,
    Alloc::ScratchArena& scratchArena
){
    outEntry = ShaderEntry(m_memoryArena);

    if(__hidden_shader_cook::CanonicalAssetType(doc).view() != __hidden_shader_cook::s_AssetTypeShader)
        return true;

    const Metascript::Value* assetValue = Assets::FindMetadataAssetMapValue<Metascript::Document, Metascript::Value>(nwbFilePath, doc, "Shader");
    if(!assetValue)
        return false;
    const Metascript::Value& asset = *assetValue;

    if(!Assets::ResolvePairedSourcePathFromMetadata(nwbFilePath, outEntry.source))
        return false;
    if(!__hidden_shader_cook::ValidatePairedSourceExtension(
        nwbFilePath,
        outEntry.source,
        __hidden_shader_cook::s_SlangSourceExtension,
        "Shader",
        scratchArena
    ))
        return false;

    if(!Assets::ReadMetadataCompactStringField(nwbFilePath, asset, "Shader meta", "stage", false, outEntry.stage))
        return false;
    outEntry.archiveStage = outEntry.stage;
    if(!Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        "Shader meta",
        [isMesh = outEntry.stage.view() == "mesh"](const AStringView field){
            return
                field == "stage" || field == "ray_query" || field == "optimization_level" || field == "entry_point"
                || field == "include_roots" || field == "defines" || (isMesh && field == "emit_mesh_compute_shadow")
            ;
        }
    ))
        return false;
    if(!__hidden_shader_cook::ParseOptionalIntegerFlagField(nwbFilePath, asset, "ray_query", outEntry.rayQuery))
        return false;
    AStringView optimizationLevelText;
    bool optimizationLevelPresent = false;
    if(!Assets::ReadMetadataStringField(
        nwbFilePath,
        asset,
        "Shader meta",
        "optimization_level",
        false,
        optimizationLevelText,
        &optimizationLevelPresent
    ))
        return false;
    if(
        optimizationLevelPresent
        && !__hidden_shader_cook::TryParseShaderOptimizationLevel(
            optimizationLevelText,
            outEntry.optimizationLevel
        )
    ){
        NWB_LOGGER_ERROR(GLB_TEXT("Shader meta '{}': unsupported optimization_level '{}'"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(optimizationLevelText)
        );
        return false;
    }
    AStringView entryPointText;
    if(!Assets::ReadMetadataStringField(nwbFilePath, asset, "Shader meta", "entry_point", false, entryPointText))
        return false;
    outEntry.entryPoint.assign(entryPointText.data(), entryPointText.size());
    if(outEntry.entryPoint.empty()){
        NWB_LOGGER_ERROR(GLB_TEXT("Shader meta '{}': entry_point must not be empty"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(!__hidden_shader_cook::ParseOptionalIntegerFlagField(nwbFilePath, asset, "emit_mesh_compute_shadow", outEntry.emitMeshComputeShadow))
        return false;

    if(const auto* includeRootsVal = asset.findField("include_roots")){
        if(!includeRootsVal->copyStringList(outEntry.includeRoots)){
            NWB_LOGGER_ERROR(GLB_TEXT("Shader meta '{}': include_roots must be a list of strings"), PathToString<tchar>(nwbFilePath));
            return false;
        }
        for(const CookString& includeRoot : outEntry.includeRoots){
            if(!includeRoot.empty())
                continue;

            NWB_LOGGER_ERROR(GLB_TEXT("Shader meta '{}': include_roots entries must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }
    }

    if(!__hidden_shader_cook::ParseDefines(nwbFilePath, asset, m_memoryArena, outEntry.defineValues))
        return false;

    if(outEntry.stage.empty()){
        NWB_LOGGER_ERROR(GLB_TEXT("Shader meta '{}': stage is required"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    return true;
}

bool ShaderCook::parseShaderMeta(const Path& nwbFilePath, ShaderEntry& outEntry, Alloc::ScratchArena& scratchArena){
    Metascript::Document doc(m_memoryArena);
    if(!parseDocument(nwbFilePath, doc))
        return false;

    return parseShaderMeta(nwbFilePath, doc, outEntry, scratchArena);
}

bool ShaderCook::parseIncludeMeta(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    IncludeEntry& outEntry,
    Alloc::ScratchArena& scratchArena
){
    outEntry = IncludeEntry(m_memoryArena);

    if(__hidden_shader_cook::CanonicalAssetType(doc).view() != __hidden_shader_cook::s_AssetTypeInclude)
        return true;

    if(!Assets::ResolvePairedSourcePathFromMetadata(nwbFilePath, outEntry.source))
        return false;
    if(!__hidden_shader_cook::ValidatePairedSourceExtension(
        nwbFilePath,
        outEntry.source,
        __hidden_shader_cook::s_SlangIncludeExtension,
        "Include",
        scratchArena
    ))
        return false;

    const Metascript::Value* assetValue = Assets::FindMetadataAssetMapValue<Metascript::Document, Metascript::Value>(nwbFilePath, doc, "Include");
    if(!assetValue)
        return false;
    const Metascript::Value& asset = *assetValue;

    if(!Assets::ValidateMetadataAssetFields(nwbFilePath, asset, "Include meta", { "defines" }))
        return false;
    if(!__hidden_shader_cook::ParseDefines(nwbFilePath, asset, m_memoryArena, outEntry.defineValues))
        return false;

    return true;
}

bool ShaderCook::parseIncludeMeta(const Path& nwbFilePath, IncludeEntry& outEntry, Alloc::ScratchArena& scratchArena){
    Metascript::Document doc(m_memoryArena);
    if(!parseDocument(nwbFilePath, doc))
        return false;

    return parseIncludeMeta(nwbFilePath, doc, outEntry, scratchArena);
}

void ShaderCook::mergeInheritedDefines(ShaderEntry& inOutEntry, const CookVector<Path>& dependencies, const CookMap<CookString, IncludeEntry>& includeMetadata){
    Alloc::ScratchArena scratchArena(AssetsShaderArenaScope::s_MergeInheritedDefinesArena);
    __hidden_shader_cook::ScratchVector<const IncludeEntry*> inheritedEntries{scratchArena};
    inheritedEntries.reserve(dependencies.size());

    usize defineReserveCount = inOutEntry.defineValues.size();
    bool canReserveDefineValues = true;
    for(const Path& dep : dependencies){
        CookString depKey = PathToString(m_memoryArena, dep);
        CanonicalizeTextInPlace(depKey);
        const auto found = includeMetadata.find(depKey);
        if(found == includeMetadata.end())
            continue;

        const IncludeEntry& includeEntry = found.value();
        inheritedEntries.push_back(&includeEntry);

        if(includeEntry.defineValues.size() > Limit<usize>::s_Max - defineReserveCount)
            canReserveDefineValues = false;
        else
            defineReserveCount += includeEntry.defineValues.size();
    }

    if(canReserveDefineValues && defineReserveCount > inOutEntry.defineValues.size())
        inOutEntry.defineValues.reserve(defineReserveCount);

    for(const IncludeEntry* includeEntry : inheritedEntries){
        for(const auto& [defineName, defineEntry] : includeEntry->defineValues){
            auto [defineIt, inserted] = inOutEntry.defineValues.try_emplace(defineName, m_memoryArena);
            if(inserted)
                defineIt.value().values.assign(defineEntry.values.begin(), defineEntry.values.end());
        }
    }
}

bool ShaderCook::gatherShaderDependencies(
    const Path& sourcePath,
    const CookVector<Path>& includeDirectories,
    CookVector<Path>& outDependencies,
    Alloc::ScratchArena& scratchArena
){
    outDependencies.clear();

    __hidden_shader_cook::ScratchHashSet<__hidden_shader_cook::ScratchString> visited{
        0,
        Hasher<__hidden_shader_cook::ScratchString>(),
        EqualTo<__hidden_shader_cook::ScratchString>(),
        scratchArena
    };
    return __hidden_shader_cook::CollectDependencies(sourcePath, includeDirectories, visited, outDependencies, scratchArena);
}

bool ShaderCook::expandDefineCombinations(
    const CookMap<CookString, DefineEntry>& defineValues,
    CookVector<DefineCombo>& outCombinations,
    Alloc::ScratchArena& scratchArena
){
    outCombinations.clear();
    DefineCombo initialCombo{0, Hasher<CookString>(), EqualTo<CookString>(), m_memoryArena};
    initialCombo.reserve(defineValues.size());
    outCombinations.push_back(Move(initialCombo));

    auto cloneComboWithEntry = [&](const DefineCombo& source, const CookString& defineName, const CookString& defineValue){
        DefineCombo copy{0, Hasher<CookString>(), EqualTo<CookString>(), m_memoryArena};
        copy.reserve(defineValues.size());
        for(const auto& [sourceName, sourceValue] : source)
            copy.try_emplace(sourceName, sourceValue);
        copy.try_emplace(defineName, defineValue);
        return copy;
    };

    for(const auto& entry : sortedDefineEntries(defineValues, scratchArena)){
        const CookString& defineName = *entry.key;
        const CookVector<CookString>& values = entry.value->values;
        if(values.empty()){
            outCombinations.clear();
            return true;
        }

        if(values.size() == 1u){
            const CookString& value = values.front();
            for(DefineCombo& combo : outCombinations){
                combo.reserve(defineValues.size());
                combo.try_emplace(defineName, value);
            }
            continue;
        }

        if(outCombinations.size() > Limit<usize>::s_Max / values.size()){
            outCombinations.clear();
            return false;
        }

        CookVector<DefineCombo> expanded{m_memoryArena};
        expanded.reserve(outCombinations.size() * values.size());

        const usize copiedValueCount = values.size() - 1u;
        for(DefineCombo& combo : outCombinations){
            for(usize valueIndex = 0u; valueIndex < copiedValueCount; ++valueIndex)
                expanded.push_back(cloneComboWithEntry(combo, defineName, values[valueIndex]));

            combo.reserve(defineValues.size());
            combo.try_emplace(defineName, values.back());
            expanded.push_back(Move(combo));
        }

        outCombinations = Move(expanded);
    }

    return true;
}

ShaderCook::CookString ShaderCook::buildVariantName(const DefineCombo& combo, Alloc::ScratchArena& scratchArena){
    if(combo.empty())
        return CookString(::__hidden_shader_cook::s_DefaultVariantText, m_memoryArena);

    if(combo.size() == 1u){
        const auto& [defineName, defineValue] = *combo.begin();
        CookString variantName{m_memoryArena};
        variantName.reserve(defineName.size() + defineValue.size() + 1u);
        variantName += defineName;
        variantName += '=';
        variantName += defineValue;
        return variantName;
    }

    const auto entries = sortedDefineEntries(combo, scratchArena);
    usize variantNameSize = entries.size() - 1u;
    for(const auto& entry : entries)
        variantNameSize += entry.key->size() + entry.value->size() + 1u;

    CookString variantName{m_memoryArena};
    variantName.reserve(variantNameSize);
    bool first = true;
    for(const auto& entry : entries){
        if(!first)
            variantName += ';';
        first = false;

        variantName += *entry.key;
        variantName += '=';
        variantName += *entry.value;
    }

    return variantName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

