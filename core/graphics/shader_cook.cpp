// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_cook.h"

#include "vulkan/vulkan_shader_compiler.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseDefineValues(const AStringView valueText, ShaderCook::CookVector<AString>& outValues){
    outValues.clear();

    usize begin = 0;
    while(begin <= valueText.size()){
        usize commaPos = valueText.find(',', begin);
        if(commaPos == AStringView::npos)
            commaPos = valueText.size();

        const AString value = Trim(valueText.substr(begin, commaPos - begin));
        if(value.empty())
            return false;

        outValues.push_back(value);
        if(commaPos == valueText.size())
            break;

        begin = commaPos + 1;
    }

    return true;
}

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

    static constexpr AStringView s_IncludeKeyword = "include";
    if(line.substr(cursor, s_IncludeKeyword.size()) != s_IncludeKeyword)
        return false;
    cursor += s_IncludeKeyword.size();

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
    if(FileExists(localCandidate, errorCode)){
        outPath = localCandidate;
        return true;
    }

    for(const Path& includeDirectory : includeDirectories){
        const Path includeCandidate = (includeDirectory / Path(includeName)).lexically_normal();
        if(FileExists(includeCandidate, errorCode)){
            outPath = includeCandidate;
            return true;
        }
    }

    return false;
}


template <typename ScratchArena>
static bool CollectDependencies(const Path& startPath, const ShaderCook::CookVector<Path>& includeDirectories, ShaderCook::CookHashSet<AString>& inOutVisitedPaths, ShaderCook::CookVector<Path>& inOutDependencies, ScratchArena& scratchArena){
    ErrorCode errorCode;

    Deque<Path, Alloc::ScratchAllocator<Path>> pending{Alloc::ScratchAllocator<Path>(scratchArena)};
    pending.push_back(startPath);

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

        AString sourceText;
        if(!ReadTextFile(absolutePath, sourceText)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to read dependency '{}'"),
                PathToString<tchar>(absolutePath)
            );
            return false;
        }

        inOutDependencies.push_back(absolutePath);

        AStringStream sourceStream(sourceText);
        AString line;
        while(ReadTextLine(sourceStream, line)){
            TrimTrailingCarriageReturn(line);

            AString includeName;
            if(!ExtractIncludeName(line, includeName))
                continue;

            Path includePath;
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


template <typename ScratchArena>
static bool ValidateDefaultVariant(const ShaderCook::ManifestEntry& entry, ScratchArena& scratchArena){
    if(entry.defaultVariant.empty())
        return true;

    if(entry.defaultVariant == "default"){
        if(entry.defineValues.empty())
            return true;

        NWB_LOGGER_ERROR(
            NWB_TEXT("Manifest parsing failed: entry '{}' default variant 'default' is only valid when no entry.define.* values are specified"),
            StringConvert(entry.name)
            );
        return false;
    }

    if(entry.defineValues.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Manifest parsing failed: entry '{}' default variant '{}' requires entry.define.* values to be specified"),
            StringConvert(entry.name),
            StringConvert(entry.defaultVariant)
        );
        return false;
    }

    HashSet<Name, Hasher<Name>, EqualTo<Name>, Alloc::ScratchAllocator<Name>> seenDefines{Alloc::ScratchAllocator<Name>(scratchArena)};
    usize begin = 0;
    while(begin < entry.defaultVariant.size()){
        usize segmentEnd = entry.defaultVariant.find(';', begin);
        if(segmentEnd == AString::npos)
            segmentEnd = entry.defaultVariant.size();

        const AString segment = Trim(AStringView(entry.defaultVariant).substr(begin, segmentEnd - begin));
        if(segment.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Manifest parsing failed: entry '{}' default variant '{}' has invalid empty segment"),
                StringConvert(entry.name),
                StringConvert(entry.defaultVariant)
            );
            return false;
        }

        const usize equalPos = segment.find('=');
        if(equalPos == AString::npos || equalPos == 0 || equalPos + 1 >= segment.size()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Manifest parsing failed: entry '{}' default variant '{}' has invalid assignment '{}'"),
                StringConvert(entry.name),
                StringConvert(entry.defaultVariant),
                StringConvert(segment)
            );
            return false;
        }

        const AString defineName = Trim(AStringView(segment).substr(0, equalPos));
        const AString defineValue = Trim(AStringView(segment).substr(equalPos + 1));
        if(defineName.empty() || defineValue.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Manifest parsing failed: entry '{}' default variant '{}' has invalid assignment '{}'"),
                StringConvert(entry.name),
                StringConvert(entry.defaultVariant),
                StringConvert(segment)
            );
            return false;
        }
        const auto defineIt = entry.defineValues.find(Name(defineName.c_str()));
        if(defineIt == entry.defineValues.end()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Manifest parsing failed: entry '{}' default variant '{}' references unknown define '{}'"),
                StringConvert(entry.name),
                StringConvert(entry.defaultVariant),
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
                NWB_TEXT("Manifest parsing failed: entry '{}' default variant '{}' has unsupported value '{}' for define '{}'"),
                StringConvert(entry.name),
                StringConvert(entry.defaultVariant),
                StringConvert(defineValue),
                StringConvert(defineName)
            );
            return false;
        }

        if(!seenDefines.insert(Name(defineName.c_str())).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Manifest parsing failed: entry '{}' default variant '{}' assigns define '{}' more than once"),
                StringConvert(entry.name),
                StringConvert(entry.defaultVariant),
                StringConvert(defineName)
            );
            return false;
        }

        begin = segmentEnd + 1;
    }

    if(seenDefines.size() != entry.defineValues.size()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Manifest parsing failed: entry '{}' default variant '{}' must assign all defines"),
            StringConvert(entry.name),
            StringConvert(entry.defaultVariant)
        );
        return false;
    }

    return true;
}

template <typename ScratchArena>
static bool ValidateManifestEntries(const ShaderCook::CookVector<ShaderCook::ManifestEntry>& entries, ScratchArena& scratchArena){
    HashSet<Name, Hasher<Name>, EqualTo<Name>, Alloc::ScratchAllocator<Name>> uniqueEntryNames{Alloc::ScratchAllocator<Name>(scratchArena)};
    for(const ShaderCook::ManifestEntry& entry : entries){
        const Name entryName(entry.name.c_str());
        if(!uniqueEntryNames.insert(entryName).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Manifest parsing failed: duplicated entry.name '{}' is not allowed"),
                StringConvert(entry.name)
            );
            return false;
        }
        if(!ValidateDefaultVariant(entry, scratchArena))
            return false;
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


bool ShaderCook::parseManifestFile(const Path& manifestPath, ManifestData& outManifest){
    outManifest = ManifestData(m_memoryArena);

    AString manifestText;
    if(!ReadTextFile(manifestPath, manifestText)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Failed to read manifest '{}'"),
            PathToString<tchar>(manifestPath)
        );
        return false;
    }
    StripUtf8Bom(manifestText);

    bool parsingEntry = false;
    ManifestEntry currentEntry(m_memoryArena);

    AStringStream stream(manifestText);
    AString line;
    usize lineNumber = 0;
    while(ReadTextLine(stream, line)){
        ++lineNumber;

        TrimTrailingCarriageReturn(line);

        const AString trimmed = Trim(line);
        if(trimmed.empty())
            continue;
        if(trimmed[0] == '#')
            continue;

        if(trimmed == "entry.begin"){
            if(parsingEntry){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Manifest line {}: nested entry.begin is not allowed"),
                    lineNumber
                );
                return false;
            }

            parsingEntry = true;
            currentEntry = ManifestEntry(m_memoryArena);
            continue;
        }
        if(trimmed == "entry.end"){
            if(!parsingEntry){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Manifest line {}: entry.end without entry.begin"),
                    lineNumber
                );
                return false;
            }

            if(currentEntry.name.empty() || currentEntry.stage.empty() || currentEntry.source.empty()){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Manifest line {}: entry requires name/stage/source (entry.name, entry.stage, entry.source)"),
                    lineNumber
                );
                return false;
            }

            currentEntry.name = CanonicalizeText(currentEntry.name);
            currentEntry.compiler = currentEntry.compiler.empty() ? AString("glslang") : CanonicalizeText(currentEntry.compiler);
            currentEntry.stage = CanonicalizeText(currentEntry.stage);
            currentEntry.targetProfile = CanonicalizeText(currentEntry.targetProfile);
            // defaultVariant contains case-sensitive preprocessor define names; do not canonicalize

            outManifest.entries.push_back(Move(currentEntry));
            parsingEntry = false;
            continue;
        }

        const usize equalPos = trimmed.find('=');
        if(equalPos == AString::npos){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Manifest line {}: expected key=value"),
                lineNumber
            );
            return false;
        }

        const AString key = Trim(AStringView(trimmed).substr(0, equalPos));
        const AString value = Trim(AStringView(trimmed).substr(equalPos + 1));
        if(key.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Manifest line {}: key is empty"),
                lineNumber
            );
            return false;
        }

        if(!parsingEntry){
            if(key == "volume.name"){
                outManifest.volumeName = CanonicalizeText(value);
                continue;
            }
            if(key == "volume.segment_size"){
                u64 parsedValue = 0;
                if(!ParseU64(value, parsedValue) || parsedValue == 0){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("Manifest line {}: invalid volume.segment_size '{}'"),
                        lineNumber,
                        StringConvert(value)
                    );
                    return false;
                }
                outManifest.segmentSize = parsedValue;
                continue;
            }
            if(key == "volume.metadata_size"){
                u64 parsedValue = 0;
                if(!ParseU64(value, parsedValue)){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("Manifest line {}: invalid volume.metadata_size '{}'"),
                        lineNumber,
                        StringConvert(value)
                    );
                    return false;
                }
                outManifest.metadataSize = parsedValue;
                continue;
            }
            if(key == "include_root"){
                if(value.empty()){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("Manifest line {}: include_root cannot be empty"),
                        lineNumber
                    );
                    return false;
                }
                outManifest.includeRoots.push_back(value);
                continue;
            }

            NWB_LOGGER_ERROR(
                NWB_TEXT("Manifest line {}: unsupported global key '{}'"),
                lineNumber,
                StringConvert(key)
            );
            return false;
        }

        if(key == "entry.name"){
            currentEntry.name = value;
            continue;
        }
        if(key == "entry.compiler"){
            currentEntry.compiler = value;
            continue;
        }
        if(key == "entry.stage"){
            currentEntry.stage = value;
            continue;
        }
        if(key == "entry.target_profile"){
            currentEntry.targetProfile = value;
            continue;
        }
        if(key == "entry.entry_point"){
            currentEntry.entryPoint = value.empty() ? AString("main") : value;
            continue;
        }
        if(key == "entry.source"){
            currentEntry.source = value;
            continue;
        }
        if(key == "entry.default_variant"){
            currentEntry.defaultVariant = value;
            continue;
        }

        static constexpr AStringView s_EntryDefinePrefix = "entry.define.";
        if(key.starts_with(s_EntryDefinePrefix)){
            const AString defineName = Trim(AStringView(key).substr(s_EntryDefinePrefix.size()));
            if(defineName.empty()){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Manifest line {}: define key is empty"),
                    lineNumber
                );
                return false;
            }

            CookVector<AString> defineValues{CookAllocator<AString>(m_memoryArena)};
            if(!__hidden_shader_cook::ParseDefineValues(value, defineValues)){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Manifest line {}: define '{}' has invalid values '{}'"),
                    lineNumber,
                    StringConvert(defineName),
                    StringConvert(value)
                );
                return false;
            }

            currentEntry.defineValues.insert_or_assign(Name(defineName.c_str()), DefineEntry{defineName, Move(defineValues)});
            continue;
        }

        NWB_LOGGER_ERROR(
            NWB_TEXT("Manifest line {}: unsupported entry key '{}'"),
            lineNumber,
            StringConvert(key)
        );
        return false;
    }

    if(parsingEntry){
        NWB_LOGGER_ERROR(NWB_TEXT("Manifest parsing failed: missing trailing entry.end"));
        return false;
    }
    if(outManifest.volumeName.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Manifest parsing failed: volume.name is empty"));
        return false;
    }
    if(outManifest.segmentSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Manifest parsing failed: volume.segment_size is zero"));
        return false;
    }
    if(outManifest.entries.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Manifest parsing failed: no entry was defined"));
        return false;
    }
    Alloc::ScratchArena<> validationArena;
    return __hidden_shader_cook::ValidateManifestEntries(outManifest.entries, validationArena);
}

bool ShaderCook::gatherShaderDependencies(const Path& sourcePath, const CookVector<Path>& includeDirectories, CookVector<Path>& outDependencies){
    Alloc::ScratchArena<> scratchArena;

    outDependencies.clear();

    CookHashSet<AString> visited{CookAllocator<AString>(m_memoryArena)};
    return __hidden_shader_cook::CollectDependencies(sourcePath, includeDirectories, visited, outDependencies, scratchArena);
}

void ShaderCook::expandDefineCombinations(const CookMap<Name, DefineEntry>& defineValues, CookVector<DefineCombo>& outCombinations){
    outCombinations.clear();
    outCombinations.push_back(DefineCombo(CookAllocator<Pair<const AString, AString>>(m_memoryArena)));

    for(const auto& entry : sortedDefineEntries(defineValues)){
        const AString& defineName = entry.value->name;
        const CookVector<AString>& values = entry.value->values;

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

bool ShaderCook::computeSourceChecksum(const ManifestEntry& entry, const AStringView variantName, const CookVector<Path>& dependencies, u64& outChecksum){
    ErrorCode errorCode;

    outChecksum = FNV64_OFFSET_BASIS;

    AString header;
    header.reserve(
        entry.name.size()
        + entry.compiler.size()
        + entry.stage.size()
        + entry.targetProfile.size()
        + entry.entryPoint.size()
        + variantName.size()
        + 6
    );
    header += entry.name;
    header += '\n';
    header += entry.compiler;
    header += '\n';
    header += entry.stage;
    header += '\n';
    header += entry.targetProfile;
    header += '\n';
    header += entry.entryPoint;
    header += '\n';
    header += variantName;
    header += '\n';

    outChecksum = UpdateFnv64(outChecksum, reinterpret_cast<const u8*>(header.data()), header.size());

    CookVector<SortedDependencyItem> sortedDependencies{CookAllocator<SortedDependencyItem>(m_memoryArena)};
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

    for(const SortedDependencyItem& item : sortedDependencies){
        AString dependencyPrefix = item.canonicalPath;
        dependencyPrefix += '\n';
        outChecksum = UpdateFnv64(
            outChecksum,
            reinterpret_cast<const u8*>(dependencyPrefix.data()),
            dependencyPrefix.size()
        );

        CookVector<u8> dependencyBytes{CookAllocator<u8>(m_memoryArena)};
        if(!ReadBinaryFile(item.path, dependencyBytes, errorCode)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to read dependency file '{}'"),
                PathToString<tchar>(item.canonicalPath)
            );
            return false;
        }
        if(!dependencyBytes.empty()){
            outChecksum = UpdateFnv64(
                outChecksum,
                dependencyBytes.data(),
                dependencyBytes.size()
            );
        }

        const u8 zeroByte = 0;
        outChecksum = UpdateFnv64(outChecksum, &zeroByte, 1);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

