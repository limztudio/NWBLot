// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_cook.h"

#include "vulkan/vulkan_shader_compiler.h"

#include <core/assets/asset_paths.h>
#include <core/metascript/parser.h>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// asset type keywords


static constexpr AStringView s_AssetTypeShader = "shader";
static constexpr AStringView s_AssetTypeInclude = "include";

static AString CanonicalAssetType(const Metascript::Document& doc){
    const auto assetType = doc.assetType();
    return CanonicalizeText(AString(assetType.data(), assetType.size()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HLSL source parsing - #include extraction and resolution


static constexpr AStringView s_HlslIncludeDirective = "include";

static bool ExtractIncludeName(const AStringView line, AString& outIncludeName){
    outIncludeName.clear();

    usize cursor = 0;
    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;

    if(cursor >= line.size() || line[cursor] != '#')
        return false;

    ++cursor;
    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;

    if(line.substr(cursor, s_HlslIncludeDirective.size()) != s_HlslIncludeDirective)
        return false;
    cursor += s_HlslIncludeDirective.size();

    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;

    if(cursor >= line.size() || line[cursor] != '"')
        return false;
    ++cursor;

    const usize closingQuote = line.find('"', cursor);
    if(closingQuote == AStringView::npos || closingQuote <= cursor)
        return false;

    outIncludeName = AString(line.substr(cursor, closingQuote - cursor));
    return !outIncludeName.empty();
}

static bool ResolveIncludeFile(const AStringView includeName, const Path& sourceDirectory, const ShaderCook::CookVector<Path>& includeDirectories, Path& outPath){
    ErrorCode errorCode;

    const Path localCandidate = (sourceDirectory / Path(includeName)).lexically_normal();
    errorCode.clear();
    if(IsRegularFile(localCandidate, errorCode)){
        outPath = localCandidate;
        return true;
    }
    if(errorCode && !IsMissingPathError(errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Failed to query include candidate '{}': {}"),
            PathToString<tchar>(localCandidate),
            StringConvert(errorCode.message())
        );
        return false;
    }

    for(const Path& includeDirectory : includeDirectories){
        const Path includeCandidate = (includeDirectory / Path(includeName)).lexically_normal();
        errorCode.clear();
        if(IsRegularFile(includeCandidate, errorCode)){
            outPath = includeCandidate;
            return true;
        }
        if(errorCode && !IsMissingPathError(errorCode)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to query include candidate '{}': {}"),
                PathToString<tchar>(includeCandidate),
                StringConvert(errorCode.message())
            );
            return false;
        }
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HLSL dependency collection


template <typename VisitedSet, typename ScratchArena>
static bool CollectDependencies(const Path& startPath, const ShaderCook::CookVector<Path>& includeDirectories, VisitedSet& inOutVisitedPaths, ShaderCook::CookVector<Path>& inOutDependencies, ScratchArena& scratchArena){
    ErrorCode errorCode;

    Deque<Path, Alloc::ScratchAllocator<Path>> pending{Alloc::ScratchAllocator<Path>(scratchArena)};
    pending.push_back(startPath);
    AString sourceText;
    AString line;
    AString includeName;
    Path includePath;

    while(!pending.empty()){
        Path dependencyPath = Move(pending.back());
        pending.pop_back();

        const Path absolutePath = AbsolutePath(dependencyPath, errorCode).lexically_normal();
        if(errorCode){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to resolve dependency path '{}' : {}"),
                PathToString<tchar>(dependencyPath),
                StringConvert(errorCode.message())
            );
            return false;
        }

        const AString canonicalPathKey = CanonicalizeText(PathToString(absolutePath));
        if(!inOutVisitedPaths.insert(canonicalPathKey).second)
            continue;

        sourceText.clear();
        if(!ReadTextFile(absolutePath, sourceText)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to read dependency '{}'"),
                PathToString<tchar>(absolutePath)
            );
            return false;
        }
        StripUtf8Bom(sourceText);

        inOutDependencies.push_back(absolutePath);

        AStringStream sourceStream(sourceText);
        while(ReadTextLine(sourceStream, line)){
            TrimTrailingCarriageReturn(line);

            includeName.clear();
            if(!ExtractIncludeName(line, includeName))
                continue;

            if(!ResolveIncludeFile(includeName, absolutePath.parent_path(), includeDirectories, includePath)){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Unable to resolve include '{}' from '{}'"),
                    StringConvert(includeName),
                    PathToString<tchar>(absolutePath)
                );
                return false;
            }

            pending.push_back(Move(includePath));
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// shader entry validation


template <typename DefineMap, typename ScratchArena>
static bool ValidateDefaultVariant(const AStringView contextLabel, const AStringView defaultVariant, const DefineMap& defineValues, ScratchArena& scratchArena){
    if(defaultVariant.empty())
        return true;

    if(defaultVariant == "default"){
        if(defineValues.empty())
            return true;

        NWB_LOGGER_ERROR(
            NWB_TEXT("Meta '{}': default variant 'default' is only valid when no defines are specified"),
            StringConvert(contextLabel)
        );
        return false;
    }

    if(defineValues.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Meta '{}': default variant '{}' requires defines to be specified"),
            StringConvert(contextLabel),
            StringConvert(defaultVariant)
        );
        return false;
    }

    HashSet<AString, Hasher<AString>, EqualTo<AString>, Alloc::ScratchAllocator<AString>> seenDefines{Alloc::ScratchAllocator<AString>(scratchArena)};
    usize begin = 0;
    while(begin < defaultVariant.size()){
        usize segmentEnd = defaultVariant.find(';', begin);
        if(segmentEnd == AString::npos)
            segmentEnd = defaultVariant.size();

        const AString segment = Trim(defaultVariant.substr(begin, segmentEnd - begin));
        if(segment.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Meta '{}': default variant '{}' has invalid empty segment"),
                StringConvert(contextLabel),
                StringConvert(defaultVariant)
            );
            return false;
        }

        const usize equalPos = segment.find('=');
        if(equalPos == AString::npos || equalPos == 0 || equalPos + 1 >= segment.size()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Meta '{}': default variant '{}' has invalid assignment '{}'"),
                StringConvert(contextLabel),
                StringConvert(defaultVariant),
                StringConvert(segment)
            );
            return false;
        }

        const AString defineName = Trim(AStringView(segment).substr(0, equalPos));
        const AString defineValue = Trim(AStringView(segment).substr(equalPos + 1));
        if(defineName.empty() || defineValue.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Meta '{}': default variant '{}' has invalid assignment '{}'"),
                StringConvert(contextLabel),
                StringConvert(defaultVariant),
                StringConvert(segment)
            );
            return false;
        }

        const auto defineIt = defineValues.find(defineName);
        if(defineIt == defineValues.end()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Meta '{}': default variant '{}' references unknown define '{}'"),
                StringConvert(contextLabel),
                StringConvert(defaultVariant),
                StringConvert(defineName)
            );
            return false;
        }

        bool valueFound = false;
        for(const AString& allowedValue : defineIt.value().values){
            if(allowedValue == defineValue){
                valueFound = true;
                break;
            }
        }
        if(!valueFound){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Meta '{}': default variant '{}' has unsupported value '{}' for define '{}'"),
                StringConvert(contextLabel),
                StringConvert(defaultVariant),
                StringConvert(defineValue),
                StringConvert(defineName)
            );
            return false;
        }

        if(!seenDefines.insert(defineName).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Meta '{}': default variant '{}' assigns define '{}' more than once"),
                StringConvert(contextLabel),
                StringConvert(defaultVariant),
                StringConvert(defineName)
            );
            return false;
        }

        begin = segmentEnd + 1;
    }

    if(seenDefines.size() != defineValues.size()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Meta '{}': default variant '{}' must assign all defines"),
            StringConvert(contextLabel),
            StringConvert(defaultVariant)
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// .nwb metadata parsing helpers


static bool RejectVirtualPathOverrideField(const Path& nwbFilePath, const Metascript::Value& asset, const AStringView assetLabel){
    if(!asset.findField("name"))
        return true;

    NWB_LOGGER_ERROR(
        NWB_TEXT("{} meta '{}': field 'name' is no longer supported; virtual paths are derived from the asset file hierarchy"),
        StringConvert(assetLabel),
        PathToString<tchar>(nwbFilePath)
    );
    return false;
}

static bool ParseNwbDocument(const Path& nwbFilePath, Metascript::Document& outDoc){
    AString metaText;
    if(!ReadTextFile(nwbFilePath, metaText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to read meta '{}'"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    StripUtf8Bom(metaText);

    if(!outDoc.parse(AStringView(metaText))){
        for(const Metascript::ParseError& err : outDoc.errors()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Meta '{}' parse error at {}:{}: {}"),
                PathToString<tchar>(nwbFilePath),
                err.line,
                err.column,
                StringConvert(AStringView(err.message.data(), err.message.size()))
            );
        }
        return false;
    }
    return true;
}

static bool ParseDefaultVariant(const Path& nwbFilePath, const Metascript::Value& asset, AString& outDefaultVariant){
    outDefaultVariant.clear();

    const auto* defaultVariantVal = asset.findField("default_variant");
    if(!defaultVariantVal)
        return true;

    if(defaultVariantVal->isList()){
        const auto& list = defaultVariantVal->asList();
        for(usize i = 0; i < list.size(); ++i){
            if(!list[i].isString()){
                NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': default_variant list elements must be strings"), PathToString<tchar>(nwbFilePath));
                return false;
            }
            if(i > 0)
                outDefaultVariant += ';';
            outDefaultVariant += list[i].copyString();
        }
    }
    else if(defaultVariantVal->isString()){
        outDefaultVariant = defaultVariantVal->copyString();
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': default_variant must be a string or list of strings"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    return true;
}

static bool ParseStringField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    AString& outValue,
    const bool canonicalize
){
    const auto* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return true;
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Meta '{}': field '{}' must be a string"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName)
        );
        return false;
    }

    outValue = fieldValue->copyString();
    if(canonicalize)
        outValue = CanonicalizeText(outValue);
    return true;
}

static bool ParseCompactStringField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    CompactString& outValue
)
{
    const auto* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return true;
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Meta '{}': field '{}' must be a string"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName)
        );
        return false;
    }

    const AStringView fieldText(fieldValue->asString().data(), fieldValue->asString().size());
    if(!outValue.assign(fieldText)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Meta '{}': field '{}' exceeds CompactString capacity"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName)
        );
        return false;
    }

    return true;
}

static bool ParseDefines(const Path& nwbFilePath, const Metascript::Value& asset, ShaderCook::CookArena& arena, ShaderCook::CookMap<AString, ShaderCook::DefineEntry>& outDefineValues){
    outDefineValues.clear();

    const auto* definesVal = asset.findField("defines");
    if(!definesVal)
        return true;

    if(!definesVal->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': defines must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    const auto& definesMap = definesVal->asMap();
    const auto defineAllocator = ShaderCook::CookAllocator<AString>(arena);
    outDefineValues.reserve(definesMap.size());
    for(const auto& [key, val] : definesMap){
        const AString defineName(key.data(), key.size());
        if(defineName.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define names must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }

        ShaderCook::CookVector<AString> defineValues(defineAllocator);
        if(!val.copyStringList(defineValues)){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' values must be a list of strings"), PathToString<tchar>(nwbFilePath), StringConvert(defineName));
            return false;
        }
        if(defineValues.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Meta '{}': define '{}' must provide at least one value"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(defineName)
            );
            return false;
        }
        for(const AString& defineValue : defineValues){
            if(!defineValue.empty())
                continue;

            NWB_LOGGER_ERROR(
                NWB_TEXT("Meta '{}': define '{}' values must not be empty"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(defineName)
            );
            return false;
        }

        ShaderCook::DefineEntry defineEntry(Move(defineValues));
        outDefineValues.insert_or_assign(defineName, Move(defineEntry));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderCook::ShaderCook(CookArena& memoryArena)
    : m_memoryArena(memoryArena)
{
    m_compiler.reset(new Vulkan::VulkanShaderCompiler(m_memoryArena));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShaderCook::parseDocument(const Path& nwbFilePath, Metascript::Document& outDoc){
    return __hidden_shader_cook::ParseNwbDocument(nwbFilePath, outDoc);
}

bool ShaderCook::validateDefaultVariant(const AStringView contextLabel, const AStringView defaultVariant, const CookMap<AString, DefineEntry>& defineValues){
    Alloc::ScratchArena<> validationArena;
    return __hidden_shader_cook::ValidateDefaultVariant(contextLabel, defaultVariant, defineValues, validationArena);
}

bool ShaderCook::parseShaderMeta(const Path& nwbFilePath, const Metascript::Document& doc, ShaderEntry& outEntry){
    outEntry = ShaderEntry(m_memoryArena);

    if(__hidden_shader_cook::CanonicalAssetType(doc) != __hidden_shader_cook::s_AssetTypeShader)
        return true;

    const Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': asset is not a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    if(!Assets::ResolvePairedSourcePathFromMetadata(nwbFilePath, outEntry.source))
        return false;

    if(!__hidden_shader_cook::RejectVirtualPathOverrideField(nwbFilePath, asset, "Shader"))
        return false;
    if(!__hidden_shader_cook::ParseCompactStringField(nwbFilePath, asset, "compiler", outEntry.compiler))
        return false;
    if(!__hidden_shader_cook::ParseCompactStringField(nwbFilePath, asset, "stage", outEntry.stage))
        return false;
    outEntry.archiveStage = outEntry.stage;
    if(!__hidden_shader_cook::ParseCompactStringField(nwbFilePath, asset, "target_profile", outEntry.targetProfile))
        return false;
    if(!__hidden_shader_cook::ParseStringField(nwbFilePath, asset, "entry_point", outEntry.entryPoint, false))
        return false;
    if(outEntry.entryPoint.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': entry_point must not be empty"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    if(!__hidden_shader_cook::ParseDefaultVariant(nwbFilePath, asset, outEntry.defaultVariant))
        return false;

    if(const auto* includeRootsVal = asset.findField("include_roots")){
        if(!includeRootsVal->copyStringList(outEntry.includeRoots)){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': include_roots must be a list of strings"), PathToString<tchar>(nwbFilePath));
            return false;
        }
        for(const AString& includeRoot : outEntry.includeRoots){
            if(!includeRoot.empty())
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': include_roots entries must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }
    }

    if(!__hidden_shader_cook::ParseDefines(nwbFilePath, asset, m_memoryArena, outEntry.defineValues))
        return false;

    if(outEntry.stage.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': stage is required"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    if(!validateDefaultVariant(PathToString(nwbFilePath), outEntry.defaultVariant, outEntry.defineValues))
        return false;
    if(!canonicalizeVariantSignature(outEntry.defaultVariant, outEntry.defaultVariant)){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': failed to canonicalize default_variant"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    return true;
}

bool ShaderCook::parseShaderMeta(const Path& nwbFilePath, ShaderEntry& outEntry){
    Metascript::Document doc(m_memoryArena);
    if(!parseDocument(nwbFilePath, doc))
        return false;

    return parseShaderMeta(nwbFilePath, doc, outEntry);
}

bool ShaderCook::parseIncludeMeta(const Path& nwbFilePath, const Metascript::Document& doc, IncludeEntry& outEntry){
    outEntry = IncludeEntry(m_memoryArena);

    if(__hidden_shader_cook::CanonicalAssetType(doc) != __hidden_shader_cook::s_AssetTypeInclude)
        return true;

    if(!Assets::ResolvePairedSourcePathFromMetadata(nwbFilePath, outEntry.source))
        return false;

    const Metascript::Value& asset = doc.asset();
    if(!asset.isMap())
        return true;

    if(!__hidden_shader_cook::ParseDefaultVariant(nwbFilePath, asset, outEntry.defaultVariant))
        return false;
    if(!__hidden_shader_cook::ParseDefines(nwbFilePath, asset, m_memoryArena, outEntry.defineValues))
        return false;

    if(!validateDefaultVariant(PathToString(nwbFilePath), outEntry.defaultVariant, outEntry.defineValues))
        return false;
    if(!canonicalizeVariantSignature(outEntry.defaultVariant, outEntry.defaultVariant)){
        NWB_LOGGER_ERROR(NWB_TEXT("Include meta '{}': failed to canonicalize default_variant"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    return true;
}

bool ShaderCook::parseIncludeMeta(const Path& nwbFilePath, IncludeEntry& outEntry){
    Metascript::Document doc(m_memoryArena);
    if(!parseDocument(nwbFilePath, doc))
        return false;

    return parseIncludeMeta(nwbFilePath, doc, outEntry);
}

void ShaderCook::mergeInheritedDefines(ShaderEntry& inOutEntry, const CookVector<Path>& dependencies, const CookMap<AString, IncludeEntry>& includeMetadata){
    for(const Path& dep : dependencies){
        const AString depKey = CanonicalizeText(PathToString(dep));
        const auto found = includeMetadata.find(depKey);
        if(found == includeMetadata.end())
            continue;

        const IncludeEntry& includeEntry = found.value();

        for(const auto& [defineName, defineEntry] : includeEntry.defineValues){
            if(inOutEntry.defineValues.find(defineName) == inOutEntry.defineValues.end()){
                inOutEntry.defineValues.insert_or_assign(defineName, DefineEntry{
                    CookVector<AString>(defineEntry.values, CookAllocator<AString>(m_memoryArena))
                });
            }
        }

        if(!includeEntry.defaultVariant.empty()){
            if(inOutEntry.defaultVariant.empty())
                inOutEntry.defaultVariant = includeEntry.defaultVariant;
            else
                inOutEntry.defaultVariant = includeEntry.defaultVariant + ";" + inOutEntry.defaultVariant;
        }
    }
}

bool ShaderCook::gatherShaderDependencies(const Path& sourcePath, const CookVector<Path>& includeDirectories, CookVector<Path>& outDependencies){
    Alloc::ScratchArena<> scratchArena;

    outDependencies.clear();

    HashSet<AString, Hasher<AString>, EqualTo<AString>, Alloc::ScratchAllocator<AString>> visited{Alloc::ScratchAllocator<AString>(scratchArena)};
    return __hidden_shader_cook::CollectDependencies(sourcePath, includeDirectories, visited, outDependencies, scratchArena);
}

bool ShaderCook::expandDefineCombinations(const CookMap<AString, DefineEntry>& defineValues, CookVector<DefineCombo>& outCombinations){
    outCombinations.clear();
    outCombinations.push_back(DefineCombo(CookAllocator<Pair<const AString, AString>>(m_memoryArena)));

    for(const auto& entry : sortedDefineEntries(defineValues)){
        const AString& defineName = *entry.key;
        const CookVector<AString>& values = entry.value->values;
        if(values.empty()){
            outCombinations.clear();
            return true;
        }

        if(outCombinations.size() > Limit<usize>::s_Max / values.size()){
            outCombinations.clear();
            return false;
        }

        CookVector<DefineCombo> expanded{CookAllocator<DefineCombo>(m_memoryArena)};
        expanded.reserve(outCombinations.size() * values.size());

        for(const DefineCombo& combo : outCombinations){
            for(const AString& value : values){
                DefineCombo copy = combo;
                copy[defineName] = value;
                expanded.push_back(Move(copy));
            }
        }

        outCombinations = Move(expanded);
    }

    return true;
}

AString ShaderCook::buildVariantName(const DefineCombo& combo){
    if(combo.empty())
        return "default";

    AString variantName;
    bool first = true;
    for(const auto& entry : sortedDefineEntries(combo)){
        if(!first)
            variantName += ';';
        first = false;

        variantName += *entry.key;
        variantName += '=';
        variantName += *entry.value;
    }

    return variantName;
}

bool ShaderCook::canonicalizeVariantSignature(const AStringView variantSignature, AString& outCanonical){
    const AString trimmedSignature = Trim(variantSignature);
    outCanonical.clear();
    if(trimmedSignature.empty())
        return true;
    if(trimmedSignature == "default"){
        outCanonical = "default";
        return true;
    }

    DefineCombo assignments{CookAllocator<Pair<const AString, AString>>(m_memoryArena)};

    usize begin = 0;
    while(begin < trimmedSignature.size()){
        usize segmentEnd = trimmedSignature.find(';', begin);
        if(segmentEnd == AString::npos)
            segmentEnd = trimmedSignature.size();

        const AString segment = Trim(trimmedSignature.substr(begin, segmentEnd - begin));
        if(segment.empty())
            return false;

        const usize equalPos = segment.find('=');
        if(equalPos == AString::npos || equalPos == 0 || equalPos + 1 >= segment.size())
            return false;

        const AString defineName = Trim(AStringView(segment).substr(0, equalPos));
        const AString defineValue = Trim(AStringView(segment).substr(equalPos + 1));
        if(defineName.empty() || defineValue.empty())
            return false;
        if(assignments.find(defineName) != assignments.end())
            return false;

        assignments.insert_or_assign(defineName, defineValue);
        begin = segmentEnd + 1;
    }

    outCanonical = buildVariantName(assignments);
    return true;
}

bool ShaderCook::computeDependencyChecksum(const CookVector<Path>& dependencies, u64& outChecksum){
    ErrorCode errorCode;
    static constexpr u8 s_NewlineByte = '\n';
    static constexpr u8 s_ZeroByte = 0;
    Alloc::ScratchArena<> scratchArena;

    outChecksum = FNV64_OFFSET_BASIS;

    Vector<SortedDependencyItem, Alloc::ScratchAllocator<SortedDependencyItem>> sortedDependencies{Alloc::ScratchAllocator<SortedDependencyItem>(scratchArena)};
    sortedDependencies.reserve(dependencies.size());
    for(const Path& dependency : dependencies){
        SortedDependencyItem item;
        item.canonicalPath = CanonicalizeText(PathToString(dependency));
        item.path = dependency;
        sortedDependencies.push_back(Move(item));
    }

    Sort(sortedDependencies.begin(), sortedDependencies.end(),
        [](const SortedDependencyItem& lhs, const SortedDependencyItem& rhs){
            return lhs.canonicalPath < rhs.canonicalPath;
        }
    );

    Vector<u8, Alloc::ScratchAllocator<u8>> dependencyBytes{Alloc::ScratchAllocator<u8>(scratchArena)};
    for(const SortedDependencyItem& item : sortedDependencies){
        outChecksum = UpdateFnv64TextExact(outChecksum, AStringView(item.canonicalPath));
        outChecksum = UpdateFnv64(outChecksum, &s_NewlineByte, 1);

        dependencyBytes.clear();
        errorCode.clear();
        if(!ReadBinaryFile(item.path, dependencyBytes, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Failed to read dependency file '{}' : {}"),
                    PathToString<tchar>(item.path),
                    StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Failed to read dependency file '{}'"),
                    PathToString<tchar>(item.path)
                );
            }
            return false;
        }
        if(!dependencyBytes.empty()){
            outChecksum = UpdateFnv64(
                outChecksum,
                dependencyBytes.data(),
                dependencyBytes.size()
            );
        }

        outChecksum = UpdateFnv64(outChecksum, &s_ZeroByte, 1);
    }

    return true;
}

bool ShaderCook::computeSourceChecksum(const ShaderEntry& entry, const AStringView variantSignature, const u64 dependencyChecksum, u64& outChecksum){
    static constexpr AStringView s_ChecksumVersionTag = "shader-source-v2";
    const u8 newlineByte = '\n';

    outChecksum = FNV64_OFFSET_BASIS;

    const auto appendChecksumLine = [&outChecksum, &newlineByte](const AStringView text){
        outChecksum = UpdateFnv64TextExact(outChecksum, text);
        outChecksum = UpdateFnv64(outChecksum, &newlineByte, 1);
    };

    appendChecksumLine(s_ChecksumVersionTag);
    appendChecksumLine(AStringView(entry.name));
    appendChecksumLine(entry.compiler.view());
    appendChecksumLine(entry.stage.view());
    appendChecksumLine(entry.archiveStage.view());
    appendChecksumLine(entry.targetProfile.view());
    appendChecksumLine(AStringView(entry.entryPoint));
    appendChecksumLine(variantSignature);
    for(const auto& entryDefine : sortedDefineEntries(entry.implicitDefines)){
        appendChecksumLine(*entryDefine.key);
        appendChecksumLine(*entryDefine.value);
    }
    outChecksum = UpdateFnv64(outChecksum, reinterpret_cast<const u8*>(&dependencyChecksum), sizeof(dependencyChecksum));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
