// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_asset_cooker.h"

#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_cook.h>

#include <core/filesystem/filesystem.h>
#include <core/metascript/parser.h>
#include <core/alloc/core.h>
#include <core/assets/asset_auto_registration.h>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_DefaultVariant = "default";
static constexpr AStringView s_NwbExtension = ".nwb";
static constexpr AStringView s_ShaderAssetType = "shader";
static constexpr AStringView s_IncludeAssetType = "include";
static constexpr AStringView s_VolumeName = "graphics";
static constexpr u64 s_DefaultSegmentSize = 16ull * 1024ull * 1024ull;
static constexpr u64 s_DefaultMetadataSize = 512ull * 1024ull;


UniquePtr<Core::Assets::IAssetCooker> CreateShaderAssetCooker(Core::Alloc::CustomArena& arena){
    return MakeUnique<ShaderAssetCooker>(arena);
}
Core::Assets::AssetCookerAutoRegistrar s_ShaderAssetCookerAutoRegistrar(&CreateShaderAssetCooker);


static AString NormalizeVariantName(const Core::ShaderCook::ShaderEntry& entry, const AStringView generatedVariantName){
    AString canonicalGeneratedVariantName = CanonicalizeText(generatedVariantName);
    if(canonicalGeneratedVariantName.empty())
        canonicalGeneratedVariantName = s_DefaultVariant;

    if(entry.defaultVariant.empty())
        return canonicalGeneratedVariantName;

    const AString canonicalDefaultVariantName = CanonicalizeText(entry.defaultVariant);
    if(canonicalGeneratedVariantName == canonicalDefaultVariantName)
        return AString(s_DefaultVariant);

    return canonicalGeneratedVariantName;
}


struct ResolvedCookPaths{
    Path repoRoot;
    Vector<Path> assetRoots;
    Path outputDirectory;
    Path cacheDirectory;
};
struct VariantCachePaths{
    Path bytecodePath;
    Path sourceChecksumPath;
};
struct PreparedShaderEntry{
    Core::ShaderCook::ShaderEntry entry;
    Path sourcePath;
    Core::ShaderCook::CookVector<Path> includeDirectories;
    Core::ShaderCook::CookVector<Path> dependencies;
    u64 dependencyChecksum = 0;
    u64 variantCount = 0;

    explicit PreparedShaderEntry(Core::ShaderCook::CookArena& arena)
        : entry(arena)
        , includeDirectories(Core::ShaderCook::CookAllocator<Path>(arena))
        , dependencies(Core::ShaderCook::CookAllocator<Path>(arena))
    {}
};
using StagedVolumePaths = StagedDirectoryPaths;

static void RemoveDirectoryIfPresentBestEffort(const Path& directoryPath, const AStringView label);

struct StageDirectoryCleanupGuard{
    Path directoryPath;
    bool active = true;

    ~StageDirectoryCleanupGuard(){
        if(active)
            RemoveDirectoryIfPresentBestEffort(directoryPath, "stage directory");
    }
};

static StagedVolumePaths BuildStagedVolumePaths(const Path& outputDirectory, const AStringView volumeName, const AStringView configurationSafeName){
    const AString outputHashHex = FormatHex64(ComputeFnv64Text(PathToString(outputDirectory)));
    const AString stageToken = StringFormat(
        "{}_{}_{}",
        BuildSafeCacheName(CanonicalizeText(volumeName)),
        configurationSafeName,
        outputHashHex
    );
    return BuildStagedDirectoryPaths(outputDirectory, stageToken);
}

static bool RemoveDirectoryIfPresent(const Path& directoryPath, const AStringView label){
    ErrorCode errorCode;

    if(!RemoveAllIfExists(directoryPath, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to remove {} '{}': {}"),
            StringConvert(label),
            PathToString<tchar>(directoryPath),
            StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}

static void RemoveDirectoryIfPresentBestEffort(const Path& directoryPath, const AStringView label){
    ErrorCode errorCode;

    if(!RemoveAllIfExists(directoryPath, errorCode) && errorCode){
        NWB_LOGGER_WARNING(
            NWB_TEXT("ShaderAssetCooker: failed to remove {} '{}': {}"),
            StringConvert(label),
            PathToString<tchar>(directoryPath),
            StringConvert(errorCode.message())
        );
    }
}

static bool EnsureEmptyDirectory(const Path& directoryPath, const AStringView label){
    ErrorCode errorCode;

    if(!::EnsureEmptyDirectory(directoryPath, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to create {} '{}': {}"),
            StringConvert(label),
            PathToString<tchar>(directoryPath),
            StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}

static bool ResolveCookPaths(const ShaderCookEnvironment& environment, ResolvedCookPaths& outPaths){
    ErrorCode errorCode;

    outPaths = {};

    if(environment.assetRoots.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: no asset roots specified"));
        return false;
    }
    if(environment.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: output directory is empty"));
        return false;
    }

    outPaths.repoRoot = environment.repoRoot.empty() ? Path(".") : environment.repoRoot;
    outPaths.repoRoot = AbsolutePath(outPaths.repoRoot, errorCode).lexically_normal();
    if(errorCode){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to resolve repo root: {}"),
            StringConvert(errorCode.message())
        );
        return false;
    }

    outPaths.assetRoots.reserve(environment.assetRoots.size());
    for(const Path& assetRoot : environment.assetRoots){
        Path resolvedAssetRoot;
        if(!ResolveAbsolutePath(outPaths.repoRoot, PathToString(assetRoot), resolvedAssetRoot, errorCode)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to resolve asset root '{}'"),
                PathToString<tchar>(assetRoot)
            );
            return false;
        }
        outPaths.assetRoots.push_back(Move(resolvedAssetRoot));
    }

    if(!ResolveAbsolutePath(outPaths.repoRoot, PathToString(environment.outputDirectory), outPaths.outputDirectory, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to resolve output directory '{}'"),
            PathToString<tchar>(environment.outputDirectory)
        );
        return false;
    }

    const Path defaultCacheDirectory = outPaths.repoRoot / "__build_obj/shader_cache";
    const Path requestedCacheDirectory = environment.cacheDirectory.empty()
        ? defaultCacheDirectory
        : environment.cacheDirectory;
    if(!ResolveAbsolutePath(outPaths.repoRoot, PathToString(requestedCacheDirectory), outPaths.cacheDirectory, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to resolve cache directory '{}'"),
            PathToString<tchar>(requestedCacheDirectory)
        );
        return false;
    }

    if(!CreateDirectories(outPaths.cacheDirectory, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to create cache directory '{}': {}"),
            PathToString<tchar>(outPaths.cacheDirectory),
            StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}


static bool DiscoverNwbFiles(const Vector<Path>& assetRoots, Vector<Path>& outNwbFiles){
    ErrorCode errorCode;
    HashSet<AString> seenNwbPaths;

    outNwbFiles.clear();

    for(const Path& assetRoot : assetRoots){
        errorCode.clear();
        if(!IsDirectory(assetRoot, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ShaderAssetCooker: failed to query asset root '{}': {}"),
                    PathToString<tchar>(assetRoot),
                    StringConvert(errorCode.message())
                );
                return false;
            }

            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: asset root is not a directory: '{}'"),
                PathToString<tchar>(assetRoot)
            );
            return false;
        }

        for(const auto& dirEntry : RecursiveDirectoryIterator(assetRoot, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ShaderAssetCooker: error scanning asset root '{}': {}"),
                    PathToString<tchar>(assetRoot),
                    StringConvert(errorCode.message())
                );
                return false;
            }

            errorCode.clear();
            const bool isRegularFile = dirEntry.is_regular_file(errorCode);
            if(errorCode){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ShaderAssetCooker: failed to inspect '{}' while scanning '{}': {}"),
                    PathToString<tchar>(dirEntry.path()),
                    PathToString<tchar>(assetRoot),
                    StringConvert(errorCode.message())
                );
                return false;
            }
            if(!isRegularFile)
                continue;

            const Path& filePath = dirEntry.path();
            const AString extension = CanonicalizeText(PathToString(filePath.extension()));
            if(extension != s_NwbExtension)
                continue;

            const AString normalizedPath = CanonicalizeText(PathToString(filePath.lexically_normal()));
            if(!seenNwbPaths.insert(normalizedPath).second)
                continue;

            outNwbFiles.push_back(filePath);
        }
    }

    Sort(outNwbFiles.begin(), outNwbFiles.end(),
        [](const Path& lhs, const Path& rhs){
            return CanonicalizeText(PathToString(lhs.lexically_normal()))
                < CanonicalizeText(PathToString(rhs.lexically_normal()));
        }
    );

    return true;
}


static bool BuildIncludeDirectories(const Path& repoRoot, const Core::ShaderCook::ShaderEntry& entry, Core::ShaderCook::CookVector<Path>& outIncludeDirectories){
    ErrorCode errorCode;

    outIncludeDirectories.clear();
    outIncludeDirectories.reserve(entry.includeRoots.size());

    for(const AString& includeRoot : entry.includeRoots){
        Path includeDirectory;
        if(!ResolveAbsolutePath(repoRoot, includeRoot, includeDirectory, errorCode)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to resolve include root '{}' for entry '{}': {}"),
                StringConvert(includeRoot),
                StringConvert(entry.name),
                StringConvert(errorCode.message())
            );
            return false;
        }

        errorCode.clear();
        const bool isDirectory = IsDirectory(includeDirectory, errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to query include root '{}' for entry '{}': {}"),
                PathToString<tchar>(includeDirectory),
                StringConvert(entry.name),
                StringConvert(errorCode.message())
            );
            return false;
        }
        if(!isDirectory){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: include root is not a directory for entry '{}': '{}'"),
                StringConvert(entry.name),
                PathToString<tchar>(includeDirectory)
            );
            return false;
        }

        outIncludeDirectories.push_back(Move(includeDirectory));
    }

    return true;
}

static bool CountShaderVariants(const Core::ShaderCook::ShaderEntry& entry, u64& outVariantCount){
    outVariantCount = 1;

    for(const auto& [_, defineEntry] : entry.defineValues){
        const u64 valueCount = static_cast<u64>(defineEntry.values.size());
        if(valueCount == 0){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: entry '{}' has define '{}' with no values"),
                StringConvert(entry.name),
                StringConvert(defineEntry.name)
            );
            return false;
        }
        if(outVariantCount > Limit<u64>::s_Max / valueCount){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: variant count overflow for entry '{}'"),
                StringConvert(entry.name)
            );
            return false;
        }
        outVariantCount *= valueCount;
    }

    return true;
}

static u64 EstimateRequiredMetadataBytes(const u64 fileCount){
    if(fileCount == 0)
        return s_DefaultMetadataSize;

    u64 indexBytes = 0;
    if(fileCount > Limit<u64>::s_Max / static_cast<u64>(sizeof(Core::Filesystem::VolumeIndexEntryDisk)))
        return Limit<u64>::s_Max;
    indexBytes = fileCount * static_cast<u64>(sizeof(Core::Filesystem::VolumeIndexEntryDisk));

    u64 totalBytes = static_cast<u64>(sizeof(Core::Filesystem::VolumeHeaderDisk));
    if(totalBytes > Limit<u64>::s_Max - indexBytes)
        return Limit<u64>::s_Max;
    totalBytes += indexBytes;

    constexpr u64 s_MetadataPaddingBytes = 4ull * 1024ull;
    if(totalBytes <= Limit<u64>::s_Max - s_MetadataPaddingBytes)
        totalBytes += s_MetadataPaddingBytes;

    return Max(totalBytes, s_DefaultMetadataSize);
}

static bool ConfigureVolumeSizing(const u64 plannedFileCount, Core::Filesystem::VolumeBuildConfig& outConfig){
    outConfig.volumeName = AString(s_VolumeName);
    outConfig.metadataSize = EstimateRequiredMetadataBytes(plannedFileCount);
    if(outConfig.metadataSize == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: metadata size overflow while planning volume"));
        return false;
    }

    outConfig.segmentSize = s_DefaultSegmentSize;
    while(outConfig.segmentSize <= outConfig.metadataSize){
        if(outConfig.segmentSize > Limit<u64>::s_Max / 2ull){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: segment size overflow while planning volume"));
            return false;
        }
        outConfig.segmentSize *= 2ull;
    }

    return true;
}


static VariantCachePaths BuildVariantCachePaths(const Path& cacheDirectory, const AStringView configurationSafeName, const Core::ShaderCook::ShaderEntry& entry, const AStringView variantName){
    const AString shaderSafeName = BuildSafeCacheName(entry.name);
    const AString stageSafeName = BuildSafeCacheName(entry.stage);
    const AString variantHashHex = FormatHex64(ComputeFnv64Text(variantName));

    VariantCachePaths cachePaths;
    cachePaths.bytecodePath = cacheDirectory / configurationSafeName / StringFormat(
        "{}__{}__{}.spv",
        shaderSafeName,
        stageSafeName,
        variantHashHex
    );
    cachePaths.sourceChecksumPath = cachePaths.bytecodePath;
    cachePaths.sourceChecksumPath += ".source";
    return cachePaths;
}


static bool GetVariantBytecode(
    const Core::ShaderCook::ShaderEntry& entry,
    const AStringView variantName,
    const Core::ShaderCook::DefineCombo& defineCombo,
    const Core::ShaderCook::CookVector<Path>& includeDirectories,
    const Path& sourcePath,
    const VariantCachePaths& cachePaths,
    const AStringView sourceChecksumHex,
    Core::ShaderCook& shaderCook,
    Vector<u8>& outBytecode
)
{
    ErrorCode errorCode;

    outBytecode.clear();

    errorCode.clear();
    const bool hasCachedBytecode = FileExists(cachePaths.bytecodePath, errorCode);
    if(errorCode){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to query bytecode cache '{}' for entry '{}': {}"),
            PathToString<tchar>(cachePaths.bytecodePath),
            StringConvert(entry.name),
            StringConvert(errorCode.message())
        );
        return false;
    }

    errorCode.clear();
    const bool hasCachedChecksum = FileExists(cachePaths.sourceChecksumPath, errorCode);
    if(errorCode){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to query checksum cache '{}' for entry '{}': {}"),
            PathToString<tchar>(cachePaths.sourceChecksumPath),
            StringConvert(entry.name),
            StringConvert(errorCode.message())
        );
        return false;
    }

    bool cacheUpToDate = false;
    if(hasCachedBytecode && hasCachedChecksum){
        AString cachedText;
        cacheUpToDate = ReadTextFile(cachePaths.sourceChecksumPath, cachedText) && (Trim(cachedText) == sourceChecksumHex);
    }
    if(cacheUpToDate){
        errorCode.clear();
        if(ReadBinaryFile(cachePaths.bytecodePath, outBytecode, errorCode) && !outBytecode.empty() && (outBytecode.size() & 3u) == 0u)
            return true;
        outBytecode.clear();
    }

    const Core::ShaderCompilerRequest compileRequest = {
        entry.name,
        entry.compiler,
        entry.stage,
        entry.targetProfile,
        entry.entryPoint,
        variantName,
        defineCombo,
        includeDirectories,
        sourcePath
    };
    if(!shaderCook.compileVariant(compileRequest, outBytecode))
        return false;

    errorCode.clear();
    if(!CreateDirectories(cachePaths.bytecodePath.parent_path(), errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to create cache directory '{}': {}"),
            PathToString<tchar>(cachePaths.bytecodePath.parent_path()),
            StringConvert(errorCode.message())
        );
        return false;
    }

    if(!WriteBinaryFile(cachePaths.bytecodePath, outBytecode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Failed to write shader bytecode cache '{}' for entry '{}'"),
            PathToString<tchar>(cachePaths.bytecodePath),
            StringConvert(entry.name)
        );
        return false;
    }

    if(!WriteTextFile(cachePaths.sourceChecksumPath, sourceChecksumHex)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Failed to write cook source checksum '{}'"),
            PathToString<tchar>(cachePaths.sourceChecksumPath)
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShaderAssetCooker::cook(const Core::Assets::AssetCookOptions& options){
    ShaderCookEnvironment environment;
    environment.configuration = options.configuration;
    environment.repoRoot = options.repoRoot.empty() ? Path(".") : Path(options.repoRoot);
    environment.assetRoots.reserve(options.assetRoots.size());
    for(const AString& assetRoot : options.assetRoots)
        environment.assetRoots.push_back(Path(assetRoot));
    environment.outputDirectory = Path(options.outputDirectory);
    environment.cacheDirectory = options.cacheDirectory.empty() ? Path() : Path(options.cacheDirectory);

    ShaderCookResult result;
    if(!cookShaderAssets(environment, result))
        return false;

    NWB_LOGGER_INFO(
        NWB_TEXT("Shader cook complete [{}] - volume='{}', files={}, segments={}, mount='{}'"),
        StringConvert(options.configuration),
        StringConvert(result.volumeName),
        result.fileCount,
        result.segmentCount,
        PathToString<tchar>(environment.outputDirectory)
    );

    return true;
}


bool ShaderAssetCooker::cookShaderAssets(const ShaderCookEnvironment& environment, ShaderCookResult& outResult){
    outResult = {};

    Core::ShaderCook shaderCook(m_arena);

    __hidden_assets::ResolvedCookPaths resolvedPaths;
    if(!__hidden_assets::ResolveCookPaths(environment, resolvedPaths))
        return false;

    // discover .nwb files from asset roots
    Vector<Path> nwbFiles;
    if(!__hidden_assets::DiscoverNwbFiles(resolvedPaths.assetRoots, nwbFiles))
        return false;

    // phase 1: parse all .nwb files and collect shader/include metadata
    Core::ShaderCook::CookMap<AString, Core::ShaderCook::IncludeEntry> includeMetadata{Core::ShaderCook::CookAllocator<Pair<const AString, Core::ShaderCook::IncludeEntry>>(m_arena)};
    Core::ShaderCook::CookVector<Core::ShaderCook::ShaderEntry> entries{Core::ShaderCook::CookAllocator<Core::ShaderCook::ShaderEntry>(m_arena)};
    for(const Path& nwbFile : nwbFiles){
        Core::Metascript::Document doc(m_arena);
        if(!shaderCook.parseDocument(nwbFile, doc))
            return false;

        const AString assetType = CanonicalizeText(AString(doc.assetType().data(), doc.assetType().size()));
        if(assetType == __hidden_assets::s_ShaderAssetType){
            Core::ShaderCook::ShaderEntry shaderEntry(m_arena);
            if(!shaderCook.parseShaderMeta(nwbFile, doc, shaderEntry))
                return false;

            if(!shaderEntry.name.empty())
                entries.push_back(Move(shaderEntry));
            continue;
        }

        if(assetType == __hidden_assets::s_IncludeAssetType){
            Core::ShaderCook::IncludeEntry includeEntry(m_arena);
            if(!shaderCook.parseIncludeMeta(nwbFile, doc, includeEntry))
                return false;

            if(!includeEntry.source.empty() && (!includeEntry.defineValues.empty() || !includeEntry.defaultVariant.empty())){
                ErrorCode ec;
                const Path absSource = AbsolutePath(Path(includeEntry.source), ec).lexically_normal();
                if(ec){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("ShaderAssetCooker: failed to resolve include metadata source '{}' from '{}': {}"),
                        StringConvert(includeEntry.source),
                        PathToString<tchar>(nwbFile),
                        StringConvert(ec.message())
                    );
                    return false;
                }

                const AString key = CanonicalizeText(PathToString(absSource));
                if(includeMetadata.find(key) != includeMetadata.end()){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("ShaderAssetCooker: duplicate include metadata for source '{}'"),
                        PathToString<tchar>(absSource)
                    );
                    return false;
                }
                includeMetadata.insert_or_assign(key, Move(includeEntry));
            }
        }
    }

    if(entries.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: no shader entries found in asset roots"));
        return false;
    }

    AString normalizedConfiguration = CanonicalizeText(environment.configuration);
    if(normalizedConfiguration.empty())
        normalizedConfiguration = "default";
    const AString configurationSafeName = BuildSafeCacheName(normalizedConfiguration);
    Vector<Core::ShaderArchive::Record> shaderIndexRecords;
    Core::ShaderCook::CookHashSet<NameHash> seenVirtualPathHashes{Core::ShaderCook::CookAllocator<NameHash>(m_arena)};

    ErrorCode errorCode;
    Vector<__hidden_assets::PreparedShaderEntry, Core::ShaderCook::CookAllocator<__hidden_assets::PreparedShaderEntry>> preparedEntries{
        Core::ShaderCook::CookAllocator<__hidden_assets::PreparedShaderEntry>(m_arena)
    };
    preparedEntries.reserve(entries.size());

    u64 plannedFileCount = 1; // shader archive index
    for(Core::ShaderCook::ShaderEntry& entry : entries){
        __hidden_assets::PreparedShaderEntry preparedEntry(m_arena);
        preparedEntry.entry = Move(entry);

        if(!ResolveAbsolutePath(resolvedPaths.repoRoot, preparedEntry.entry.source, preparedEntry.sourcePath, errorCode)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to resolve source path '{}' for entry '{}'"),
                StringConvert(preparedEntry.entry.source),
                StringConvert(preparedEntry.entry.name)
            );
            return false;
        }

        errorCode.clear();
        const bool sourceExists = FileExists(preparedEntry.sourcePath, errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to query source path '{}' for entry '{}': {}"),
                PathToString<tchar>(preparedEntry.sourcePath),
                StringConvert(preparedEntry.entry.name),
                StringConvert(errorCode.message())
            );
            return false;
        }
        if(!sourceExists){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Shader source does not exist for entry '{}': '{}'"),
                StringConvert(preparedEntry.entry.name),
                PathToString<tchar>(preparedEntry.sourcePath)
            );
            return false;
        }

        errorCode.clear();
        const bool isRegularSourceFile = IsRegularFile(preparedEntry.sourcePath, errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to inspect source path '{}' for entry '{}': {}"),
                PathToString<tchar>(preparedEntry.sourcePath),
                StringConvert(preparedEntry.entry.name),
                StringConvert(errorCode.message())
            );
            return false;
        }
        if(!isRegularSourceFile){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Shader source is not a regular file for entry '{}': '{}'"),
                StringConvert(preparedEntry.entry.name),
                PathToString<tchar>(preparedEntry.sourcePath)
            );
            return false;
        }

        if(!__hidden_assets::BuildIncludeDirectories(resolvedPaths.repoRoot, preparedEntry.entry, preparedEntry.includeDirectories))
            return false;
        if(!shaderCook.gatherShaderDependencies(preparedEntry.sourcePath, preparedEntry.includeDirectories, preparedEntry.dependencies))
            return false;

        shaderCook.mergeInheritedDefines(preparedEntry.entry, preparedEntry.dependencies, includeMetadata);
        if(!shaderCook.validateDefaultVariant(preparedEntry.entry.name, preparedEntry.entry.defaultVariant, preparedEntry.entry.defineValues))
            return false;
        if(!shaderCook.canonicalizeVariantSignature(preparedEntry.entry.defaultVariant, preparedEntry.entry.defaultVariant)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to canonicalize merged default_variant for '{}'"),
                StringConvert(preparedEntry.entry.name)
            );
            return false;
        }
        if(!shaderCook.computeDependencyChecksum(preparedEntry.dependencies, preparedEntry.dependencyChecksum))
            return false;
        if(!__hidden_assets::CountShaderVariants(preparedEntry.entry, preparedEntry.variantCount))
            return false;
        if(plannedFileCount > Limit<u64>::s_Max - preparedEntry.variantCount){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: planned file count overflow"));
            return false;
        }
        plannedFileCount += preparedEntry.variantCount;

        preparedEntries.push_back(Move(preparedEntry));
    }

    shaderIndexRecords.reserve(static_cast<usize>(plannedFileCount - 1));

    Core::Filesystem::VolumeBuildConfig volumeConfig;
    if(!__hidden_assets::ConfigureVolumeSizing(plannedFileCount, volumeConfig))
        return false;

    const __hidden_assets::StagedVolumePaths stagedVolumePaths = __hidden_assets::BuildStagedVolumePaths(
        resolvedPaths.outputDirectory,
        volumeConfig.volumeName,
        configurationSafeName
    );
    if(!__hidden_assets::EnsureEmptyDirectory(stagedVolumePaths.stageDirectory, "stage directory"))
        return false;
    __hidden_assets::StageDirectoryCleanupGuard stageDirectoryCleanup{stagedVolumePaths.stageDirectory};
    if(!__hidden_assets::RemoveDirectoryIfPresent(stagedVolumePaths.backupDirectory, "backup directory"))
        return false;

    u64 stagedFileCount = 0;
    usize stagedSegmentCount = 0;
    {
        Core::Filesystem::VolumeSession volumeSession(m_arena);
        if(!volumeSession.create(stagedVolumePaths.stageDirectory, volumeConfig)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: failed to create staged volume session"));
            return false;
        }

        for(__hidden_assets::PreparedShaderEntry& preparedEntry : preparedEntries){
            Core::ShaderCook::ShaderEntry& entry = preparedEntry.entry;
            Core::ShaderCook::CookVector<Core::ShaderCook::DefineCombo> defineCombinations{Core::ShaderCook::CookAllocator<Core::ShaderCook::DefineCombo>(m_arena)};
            shaderCook.expandDefineCombinations(entry.defineValues, defineCombinations);
            if(defineCombinations.empty())
                defineCombinations.push_back(Core::ShaderCook::DefineCombo(Core::ShaderCook::CookAllocator<Pair<const AString, AString>>(m_arena)));

            for(const Core::ShaderCook::DefineCombo& defineCombo : defineCombinations){
                const AString generatedVariantName = shaderCook.buildVariantName(defineCombo);
                const AString variantName = __hidden_assets::NormalizeVariantName(entry, generatedVariantName);

                u64 sourceChecksum = 0;
                if(!shaderCook.computeSourceChecksum(entry, generatedVariantName, preparedEntry.dependencyChecksum, sourceChecksum))
                    return false;
                const AString sourceChecksumHex = FormatHex64(sourceChecksum);

                const __hidden_assets::VariantCachePaths cachePaths = __hidden_assets::BuildVariantCachePaths(
                    resolvedPaths.cacheDirectory,
                    configurationSafeName,
                    entry,
                    variantName
                );
                Vector<u8> cookedBytecode;
                if(!__hidden_assets::GetVariantBytecode(
                    entry,
                    variantName,
                    defineCombo,
                    preparedEntry.includeDirectories,
                    preparedEntry.sourcePath,
                    cachePaths,
                    sourceChecksumHex,
                    shaderCook,
                    cookedBytecode
                )){
                    return false;
                }

                const AString variantNameText(variantName);
                const AString virtualPath = Core::ShaderArchive::buildVirtualPath(entry.name, variantName, entry.stage);
                const NameHash virtualPathHash = Name(virtualPath.c_str()).hash();
                if(!seenVirtualPathHashes.insert(virtualPathHash).second){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("Shader cook produced duplicate virtual path '{}' (entry='{}', variant='{}')"),
                        StringConvert(virtualPath),
                        StringConvert(entry.name),
                        StringConvert(variantName)
                    );
                    return false;
                }

                if(!volumeSession.pushDataDeferred(virtualPath, cookedBytecode)){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("Failed to push shader bytecode '{}'"),
                        StringConvert(virtualPath)
                    );
                    return false;
                }

                Core::ShaderArchive::Record record;
                record.shaderName = Name(entry.name.c_str());
                record.variantName = Name(variantNameText.c_str());
                record.stage = Name(entry.stage.c_str());
                record.entryPoint = Name(entry.entryPoint.c_str());
                record.sourceChecksum = sourceChecksum;
                record.bytecodeChecksum = ComputeFnv64Bytes(cookedBytecode.data(), cookedBytecode.size());
                record.virtualPathHash = virtualPathHash;
                shaderIndexRecords.push_back(Move(record));
            }
        }

        Vector<u8> indexBinary;
        if(!Core::ShaderArchive::serializeIndex(shaderIndexRecords, indexBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: failed to serialize shader index"));
            return false;
        }
        if(!volumeSession.pushDataDeferred(Core::ShaderArchive::s_IndexVirtualPath, indexBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: failed to push shader index"));
            return false;
        }
        if(!volumeSession.flush()){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: failed to flush staged volume metadata"));
            return false;
        }

        stagedFileCount = volumeSession.fileCount();
        stagedSegmentCount = volumeSession.segmentCount();
    }

    if(!Core::Filesystem::PublishStagedVolume(stagedVolumePaths, resolvedPaths.outputDirectory, volumeConfig.volumeName, stagedSegmentCount))
        return false;
    stageDirectoryCleanup.active = false;

    outResult.volumeName = AString(__hidden_assets::s_VolumeName);
    outResult.fileCount = stagedFileCount;
    outResult.segmentCount = static_cast<u64>(stagedSegmentCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

