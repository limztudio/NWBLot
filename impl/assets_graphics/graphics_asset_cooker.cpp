// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graphics_asset_cooker.h"

#include <impl/assets_geometry/geometry_asset_cook.h>
#include <impl/assets_material/material_asset_cook.h>
#include <impl/assets_material/material_shader_stage_names.h>
#include <impl/assets_shader/shader_asset.h>
#include <impl/assets_shader/shader_cook.h>

#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_stage_names.h>

#include <core/filesystem/filesystem.h>
#include <core/metascript/parser.h>
#include <core/alloc/core.h>
#include <core/alloc/scratch.h>
#include <core/assets/asset_paths.h>
#include <core/assets/asset_auto_registration.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_asset_cooker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_VolumeName = "graphics";
static constexpr AStringView s_CookerLogPrefix = "GraphicsAssetCooker";
static constexpr u64 s_DefaultSegmentSize = 512ull * 1024ull * 1024ull;
static constexpr u64 s_DefaultMetadataSize = 512ull * 1024ull;

using CookString = ShaderCook::CookString;
template<typename T>
using CookVector = ShaderCook::CookVector<T>;
using ScratchArena = Core::Alloc::ScratchArena;
using ScratchString = AString<ScratchArena>;
using IncludeDirectoryScratchSet = HashSet<ScratchString, Hasher<ScratchString>, EqualTo<ScratchString>, ScratchArena>;


UniquePtr<Core::Assets::IAssetCooker> CreateGraphicsAssetCooker(Core::Alloc::GlobalArena& arena){
    return MakeUnique<GraphicsAssetCooker>(arena);
}
Core::Assets::AssetCookerAutoRegistrar s_GraphicsAssetCookerAutoRegistrar(&CreateGraphicsAssetCooker);

static const Name& IncludeAssetTypeName(){
    static const Name s_Name("include");
    return s_Name;
}

static constexpr AStringView s_EnabledImplicitDefineValue = "1";

static bool ValidateShaderDoesNotUseImplicitDefine(ShaderCook::ShaderEntry& entry, const AStringView defineName){
    ShaderCook::CookArena& arena = entry.name.get_allocator().arena();
    CookString defineNameKey(defineName, arena);
    if(entry.defineValues.find(defineNameKey) == entry.defineValues.end())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: shader '{}' uses reserved implicit define '{}' as a variant define")
        , StringConvert(entry.name)
        , StringConvert(defineName)
    );
    return false;
}

static bool SetShaderImplicitDefine(
    ShaderCook::ShaderEntry& entry,
    const AStringView defineName,
    const AStringView defineValue
){
    if(!ValidateShaderDoesNotUseImplicitDefine(entry, defineName))
        return false;

    ShaderCook::CookArena& arena = entry.name.get_allocator().arena();
    CookString defineNameKey(defineName, arena);
    entry.implicitDefines.insert_or_assign(Move(defineNameKey), CookString(defineValue, arena));
    return true;
}

static bool BuildMeshComputeShadowEntry(const ShaderCook::ShaderEntry& sourceEntry, ShaderCook::ShaderEntry& outEntry){
    outEntry = sourceEntry;
    if(!outEntry.archiveStage.assign(MaterialShaderStageNames::MeshComputeArchiveStageText()))
        return false;
    if(!outEntry.stage.assign("cs"))
        return false;
    if(!outEntry.targetProfile.assign(sourceEntry.targetProfile))
        return false;

    return SetShaderImplicitDefine(
        outEntry,
        MaterialShaderStageNames::MeshComputeImplicitDefineText(),
        s_EnabledImplicitDefineValue
    );
}


struct ResolvedCookPaths{
    Path repoRoot;
    ShaderCook::CookVector<Path> assetRoots;
    Path outputDirectory;
    Path cacheDirectory;

    explicit ResolvedCookPaths(ShaderCook::CookArena& arena)
        : assetRoots(arena)
    {}
};
struct DiscoveredNwbFile{
    Path assetRoot;
    Path filePath;
    CookString normalizedPathText;
    ACompactString virtualRoot;

    DiscoveredNwbFile(ShaderCook::CookArena& arena, const Path& inAssetRoot, const Path& inFilePath, AStringView inNormalizedPathText, ACompactString inVirtualRoot)
        : assetRoot(inAssetRoot)
        , filePath(inFilePath)
        , normalizedPathText(inNormalizedPathText, arena)
        , virtualRoot(inVirtualRoot)
    {}
};
struct VariantCachePaths{
    Path bytecodePath;
    Path sourceChecksumPath;
};
struct PreparedShaderKey{
    Name shaderName = NAME_NONE;
    Name stageName = NAME_NONE;
};
struct PreparedShaderKeyHasher{
    usize operator()(const PreparedShaderKey& key)const{
        usize seed = Hasher<Name>{}(key.shaderName);
        Core::CoreDetail::HashCombine(seed, key.stageName);
        return seed;
    }
};
inline bool operator==(const PreparedShaderKey& lhs, const PreparedShaderKey& rhs){
    return lhs.shaderName == rhs.shaderName && lhs.stageName == rhs.stageName;
}
struct PreparedShaderEntry{
    ShaderCook::ShaderEntry entry;
    Path sourcePath;
    ShaderCook::CookVector<Path> includeDirectories;
    ShaderCook::CookVector<Path> dependencies;
    u64 dependencyChecksum = 0;
    u64 variantCount = 0;
    CookString materialTypedBindingInterfacePath;
    Name materialTypedBindingInterface = NAME_NONE;
    bool usesMaterialTypedBinding = false;

    explicit PreparedShaderEntry(ShaderCook::CookArena& arena)
        : entry(arena)
        , includeDirectories(arena)
        , dependencies(arena)
        , materialTypedBindingInterfacePath(arena)
    {}
};
using IncludeMetadataMap = ShaderCook::CookMap<CookString, ShaderCook::IncludeEntry>;
using ShaderEntryVector = CookVector<ShaderCook::ShaderEntry>;
using PreparedShaderVector = Vector<PreparedShaderEntry, ShaderCook::CookArena>;
using VirtualPathHashSet = ShaderCook::CookHashSet<NameHash>;
using DiscoveredNwbFileVector = CookVector<DiscoveredNwbFile>;
using DiscoveredBindFileVector = CookVector<DiscoveredNwbFile>;
using GeometryCookEntryVector = CookVector<GeometryCookEntry>;
using SkinnedGeometryCookEntryVector = CookVector<SkinnedGeometryCookEntry>;
using MaterialCookEntryVector = CookVector<MaterialCookEntry>;
using MaterialBindEntryVector = CookVector<MaterialBindEntry>;

struct ParsedAssetMetadata{
    IncludeMetadataMap includeMetadata;
    ShaderEntryVector shaderEntries;
    MaterialBindEntryVector materialBindEntries;
    GeometryCookEntryVector geometryEntries;
    SkinnedGeometryCookEntryVector skinnedGeometryEntries;
    MaterialCookEntryVector materialEntries;

    explicit ParsedAssetMetadata(ShaderCook::CookArena& arena)
        : includeMetadata(arena)
        , shaderEntries(arena)
        , materialBindEntries(arena)
        , geometryEntries(arena)
        , skinnedGeometryEntries(arena)
        , materialEntries(arena)
    {}
};

struct PreparedShaderPlan{
    PreparedShaderVector preparedEntries;
    u64 plannedFileCount = 1; // shader archive index

    explicit PreparedShaderPlan(ShaderCook::CookArena& arena)
        : preparedEntries(arena)
    {}
};

template<typename EntryT, typename PathHashSetT, typename EntryVectorT>
static bool AppendUniquePropertyAssetEntry(EntryT& entry, PathHashSetT& seenPathHashes, EntryVectorT& outEntries){
    if(!entry.virtualPath)
        return true;

    const NameHash pathHash = entry.virtualPath.hash();
    if(!seenPathHashes.insert(pathHash).second){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: duplicate property asset virtual path '{}'")
            , StringConvert(entry.virtualPath.c_str())
        );
        return false;
    }

    outEntries.push_back(Move(entry));
    return true;
}

using StagedVolumePaths = StagedDirectoryPaths;

static StagedVolumePaths BuildStagedVolumePaths(
    const Path& outputDirectory,
    const AStringView volumeName,
    const AStringView configurationSafeName,
    ScratchArena& scratchArena
){
    const ScratchString volumeSafeName = BuildCanonicalSafeCacheName(scratchArena, volumeName);
    ScratchString stageToken{scratchArena};
    stageToken.reserve(volumeSafeName.size() + 1u + configurationSafeName.size() + 1u + 16u);
    stageToken += volumeSafeName;
    stageToken += '_';
    stageToken += configurationSafeName;
    stageToken += '_';
    AppendHexU64(ComputeFnv64Text(PathToString(scratchArena, outputDirectory)), stageToken);
    return BuildStagedDirectoryPaths(scratchArena, outputDirectory, stageToken);
}

static bool ResolveCookPaths(
    const GraphicsCookEnvironment& environment,
    ResolvedCookPaths& outPaths,
    ScratchArena& scratchArena
){
    ErrorCode errorCode;

    outPaths.repoRoot.clear();
    outPaths.assetRoots.clear();
    outPaths.outputDirectory.clear();
    outPaths.cacheDirectory.clear();

    if(environment.assetRoots.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: no asset roots specified"));
        return false;
    }
    if(environment.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: output directory is empty"));
        return false;
    }

    outPaths.repoRoot = environment.repoRoot.empty() ? Path(".") : environment.repoRoot;
    outPaths.repoRoot = AbsolutePath(outPaths.repoRoot, errorCode).lexically_normal();
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to resolve repo root: {}"), StringConvert(errorCode.message()));
        return false;
    }

    outPaths.assetRoots.reserve(environment.assetRoots.size());
    for(const Path& assetRoot : environment.assetRoots){
        Path resolvedAssetRoot;
        const ScratchString assetRootText = PathToString(scratchArena, assetRoot);
        errorCode.clear();
        if(!ResolveAbsolutePath(outPaths.repoRoot, assetRootText, resolvedAssetRoot, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to resolve asset root '{}': {}")
                    , PathToString<tchar>(assetRoot)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: asset root is empty or invalid: '{}'")
                    , PathToString<tchar>(assetRoot)
                );
            }
            outPaths.assetRoots.clear();
            return false;
        }
        outPaths.assetRoots.push_back(Move(resolvedAssetRoot));
    }

    errorCode.clear();
    {
        const ScratchString outputDirectoryText = PathToString(scratchArena, environment.outputDirectory);
        if(!ResolveAbsolutePath(outPaths.repoRoot, outputDirectoryText, outPaths.outputDirectory, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to resolve output directory '{}': {}")
                    , PathToString<tchar>(environment.outputDirectory)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: output directory is empty or invalid: '{}'")
                    , PathToString<tchar>(environment.outputDirectory)
                );
            }
            return false;
        }
    }

    const Path defaultCacheDirectory = outPaths.repoRoot / "__build_obj/shader_cache";
    const Path requestedCacheDirectory = environment.cacheDirectory.empty()
        ? defaultCacheDirectory
        : environment.cacheDirectory
    ;
    errorCode.clear();
    {
        const ScratchString requestedCacheDirectoryText = PathToString(scratchArena, requestedCacheDirectory);
        if(!ResolveAbsolutePath(outPaths.repoRoot, requestedCacheDirectoryText, outPaths.cacheDirectory, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to resolve cache directory '{}': {}")
                    , PathToString<tchar>(requestedCacheDirectory)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: cache directory is empty or invalid: '{}'")
                    , PathToString<tchar>(requestedCacheDirectory)
                );
            }
            return false;
        }
    }

    if(!EnsureDirectories(outPaths.cacheDirectory, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to create cache directory '{}': {}")
            , PathToString<tchar>(outPaths.cacheDirectory)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}


static bool DiscoverFilesWithExtension(
    const ShaderCook::CookVector<Path>& assetRoots,
    const AStringView expectedExtension,
    DiscoveredNwbFileVector& outFiles,
    ScratchArena& scratchArena
){
    ShaderCook::CookArena& cookArena = outFiles.get_allocator().arena();
    ErrorCode errorCode;
    HashSet<u64, Hasher<u64>, EqualTo<u64>, ScratchArena> seenPathHashes(
        0,
        Hasher<u64>(),
        EqualTo<u64>(),
        scratchArena
    );

    outFiles.clear();

    for(const Path& assetRoot : assetRoots){
        ACompactString virtualRoot;
        if(!Core::Assets::BuildAssetRootVirtualRoot(assetRoot, virtualRoot, scratchArena))
            return false;

        errorCode.clear();
        if(!IsDirectory(assetRoot, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to query asset root '{}': {}")
                    , PathToString<tchar>(assetRoot)
                    , StringConvert(errorCode.message())
                );
                return false;
            }

            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: asset root is not a directory: '{}'")
                , PathToString<tchar>(assetRoot)
            );
            return false;
        }

        for(const auto& dirEntry : RecursiveDirectoryIterator(assetRoot, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: error scanning asset root '{}': {}")
                    , PathToString<tchar>(assetRoot)
                    , StringConvert(errorCode.message())
                );
                return false;
            }

            errorCode.clear();
            const bool isRegularFile = dirEntry.is_regular_file(errorCode);
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to inspect '{}' while scanning '{}': {}")
                    , PathToString<tchar>(dirEntry.path())
                    , PathToString<tchar>(assetRoot)
                    , StringConvert(errorCode.message())
                );
                return false;
            }
            if(!isRegularFile)
                continue;

            const Path& filePath = dirEntry.path();
            ScratchString extension = PathToString(scratchArena, filePath.extension());
            CanonicalizeTextInPlace(extension);
            if(extension != expectedExtension)
                continue;

            ScratchString normalizedPath = PathToString(scratchArena, filePath.lexically_normal());
            CanonicalizeTextInPlace(normalizedPath);
            if(!seenPathHashes.insert(ComputeFnv64Text(normalizedPath)).second)
                continue;

            outFiles.emplace_back(cookArena, assetRoot, filePath, normalizedPath, virtualRoot);
        }
    }

    Sort(
        outFiles.begin(),
        outFiles.end(),
        [](const DiscoveredNwbFile& lhs, const DiscoveredNwbFile& rhs){
            return lhs.normalizedPathText < rhs.normalizedPathText;
        }
    );

    return true;
}

static bool AppendIncludeDirectory(
    const Path& includeDirectory,
    const ShaderCook::ShaderEntry& entry,
    IncludeDirectoryScratchSet& seenIncludeDirectories,
    ShaderCook::CookVector<Path>& outIncludeDirectories,
    ScratchArena& scratchArena
){
    ErrorCode errorCode;
    const bool isDirectory = IsDirectory(includeDirectory, errorCode);
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to query include root '{}' for entry '{}': {}")
            , PathToString<tchar>(includeDirectory)
            , StringConvert(entry.name)
            , StringConvert(errorCode.message())
        );
        return false;
    }
    if(!isDirectory){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: include root is not a directory for entry '{}': '{}'")
            , StringConvert(entry.name)
            , PathToString<tchar>(includeDirectory)
        );
        return false;
    }

    ScratchString normalizedIncludeDirectory = PathToString(scratchArena, includeDirectory.lexically_normal());
    CanonicalizeTextInPlace(normalizedIncludeDirectory);
    if(!seenIncludeDirectories.insert(Move(normalizedIncludeDirectory)).second)
        return true;

    outIncludeDirectories.push_back(includeDirectory);
    return true;
}

template<typename AssetRootVector>
static bool BuildIncludeDirectories(
    const Path& repoRoot,
    const AssetRootVector& assetRoots,
    const Path& materialBindIncludeRoot,
    const ShaderCook::ShaderEntry& entry,
    ShaderCook::CookVector<Path>& outIncludeDirectories,
    ScratchArena& scratchArena
){
    ErrorCode errorCode;
    IncludeDirectoryScratchSet seenIncludeDirectories(
        0,
        Hasher<ScratchString>(),
        EqualTo<ScratchString>(),
        scratchArena
    );

    outIncludeDirectories.clear();
    const usize implicitIncludeRootCount = materialBindIncludeRoot.empty() ? 0u : 1u;
    if(entry.includeRoots.size() > Limit<usize>::s_Max - implicitIncludeRootCount){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: include root count overflow for entry '{}'"), StringConvert(entry.name));
        return false;
    }

    outIncludeDirectories.reserve(entry.includeRoots.size() + implicitIncludeRootCount);
    seenIncludeDirectories.reserve(entry.includeRoots.size() + implicitIncludeRootCount);

    if(!materialBindIncludeRoot.empty()){
        if(!AppendIncludeDirectory(materialBindIncludeRoot, entry, seenIncludeDirectories, outIncludeDirectories, scratchArena))
            return false;
    }

    for(const CookString& includeRoot : entry.includeRoots){
        Path includeDirectory;
        if(!Core::Assets::ResolveVirtualAssetPath(assetRoots, includeRoot, includeDirectory, scratchArena)){
            if(Core::Assets::HasReservedAssetVirtualRoot(includeRoot, scratchArena)){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to resolve virtual include root '{}' for entry '{}'")
                    , StringConvert(includeRoot)
                    , StringConvert(entry.name)
                );
                return false;
            }

            errorCode.clear();
            if(!ResolveAbsolutePath(repoRoot, includeRoot, includeDirectory, errorCode)){
                if(errorCode){
                    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to resolve include root '{}' for entry '{}': {}")
                        , StringConvert(includeRoot)
                        , StringConvert(entry.name)
                        , StringConvert(errorCode.message())
                    );
                }
                else{
                    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: include root '{}' is empty or invalid for entry '{}'")
                        , StringConvert(includeRoot)
                        , StringConvert(entry.name)
                    );
                }
                return false;
            }
        }

        if(!AppendIncludeDirectory(includeDirectory, entry, seenIncludeDirectories, outIncludeDirectories, scratchArena))
            return false;
    }

    return true;
}

static bool CountShaderVariants(const ShaderCook::ShaderEntry& entry, u64& outVariantCount){
    outVariantCount = 1;

    for(const auto& [defineName, defineEntry] : entry.defineValues){
        const u64 valueCount = static_cast<u64>(defineEntry.values.size());
        if(valueCount == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: entry '{}' has define '{}' with no values")
                , StringConvert(entry.name)
                , StringConvert(defineName)
            );
            return false;
        }
        if(outVariantCount > Limit<u64>::s_Max / valueCount){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: variant count overflow for entry '{}'"), StringConvert(entry.name));
            return false;
        }
        outVariantCount *= valueCount;
    }

    return true;
}

static u64 EstimateRequiredMetadataBytes(const u64 fileCount){
    if(fileCount == 0)
        return s_DefaultMetadataSize;

    u64 totalBytes = 0;
    if(!Core::Filesystem::ComputeVolumeMetadataRequirement(fileCount, totalBytes))
        return Limit<u64>::s_Max;

    constexpr u64 s_MetadataPaddingBytes = 4ull * 1024ull;
    if(totalBytes <= Limit<u64>::s_Max - s_MetadataPaddingBytes)
        totalBytes += s_MetadataPaddingBytes;

    return Max(totalBytes, s_DefaultMetadataSize);
}

static bool ConfigureVolumeSizing(const u64 plannedFileCount, Core::Filesystem::VolumeBuildConfig& outConfig){
    if(!outConfig.volumeName.assign(s_VolumeName)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: volume name '{}' exceeds ACompactString capacity"), StringConvert(s_VolumeName));
        return false;
    }
    outConfig.metadataSize = EstimateRequiredMetadataBytes(plannedFileCount);
    if(outConfig.metadataSize == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: metadata size overflow while planning volume"));
        return false;
    }

    outConfig.segmentSize = s_DefaultSegmentSize;
    while(outConfig.segmentSize <= outConfig.metadataSize){
        if(outConfig.segmentSize > Limit<u64>::s_Max / 2ull){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: segment size overflow while planning volume"));
            return false;
        }
        outConfig.segmentSize *= 2ull;
    }

    return true;
}

static bool ValidateMaterialVariant(
    ShaderCook& shaderCook,
    const MaterialCookEntry& materialEntry,
    const PreparedShaderEntry& preparedShaderEntry,
    const Name& stageName,
    ScratchArena& scratchArena
){
    if(materialEntry.shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' has empty shader_variant")
            , StringConvert(materialEntry.virtualPath.c_str())
        );
        return false;
    }
    const AStringView requestedVariant(materialEntry.shaderVariant.data(), materialEntry.shaderVariant.size());

    ShaderCook::CookArena& arena = materialEntry.shaderVariant.get_allocator().arena();
    const CookString contextLabel = StringFormat(arena, "{} [{}]", materialEntry.virtualPath.c_str(), stageName.c_str());
    return shaderCook.validateVariantSignature(contextLabel, requestedVariant, preparedShaderEntry.entry.defineValues, scratchArena);
}

static bool ValidateMaterials(
    ShaderCook& shaderCook,
    const Vector<PreparedShaderEntry, ShaderCook::CookArena>& preparedEntries,
    const MaterialCookEntryVector& materialEntries,
    ScratchArena& scratchArena
){
    HashMap<
        PreparedShaderKey,
        const PreparedShaderEntry*,
        PreparedShaderKeyHasher,
        EqualTo<PreparedShaderKey>,
        ScratchArena
    > preparedShaderLookup(
        0,
        PreparedShaderKeyHasher(),
        EqualTo<PreparedShaderKey>(),
        scratchArena
    );
    preparedShaderLookup.reserve(preparedEntries.size());
    for(const PreparedShaderEntry& preparedEntry : preparedEntries){
        const PreparedShaderKey shaderKey{
            ToName(preparedEntry.entry.name),
            ToName(preparedEntry.entry.archiveStage.view())
        };
        if(!preparedShaderLookup.emplace(shaderKey, &preparedEntry).second){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: duplicate prepared shader key '{}' stage '{}'")
                , StringConvert(preparedEntry.entry.name)
                , StringConvert(preparedEntry.entry.archiveStage.c_str())
            );
            return false;
        }
    }

    for(const MaterialCookEntry& materialEntry : materialEntries){
        bool hasShaderStage = false;
        bool hasMeshShader = false;

        for(const auto& [shaderType, shaderAsset] : materialEntry.stageShaders){
            hasShaderStage = true;
            const Name& stageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
            const PreparedShaderKey shaderLookupKey{ shaderAsset.name(), stageName };
            const auto foundShader = preparedShaderLookup.find(shaderLookupKey);
            if(foundShader == preparedShaderLookup.end()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' references unknown shader '{}' for stage '{}'")
                    , StringConvert(materialEntry.virtualPath.c_str())
                    , StringConvert(shaderAsset.name().c_str())
                    , StringConvert(stageName.c_str())
                );
                return false;
            }

            if(!ValidateMaterialVariant(shaderCook, materialEntry, *foundShader.value(), stageName, scratchArena))
                return false;

            if(shaderType == Core::ShaderType::MeshStage){
                hasMeshShader = true;
                const Name shaderMaterialInterface = foundShader.value()->materialTypedBindingInterface;
                const CookString& shaderMaterialInterfacePath = foundShader.value()->materialTypedBindingInterfacePath;
                if(!shaderMaterialInterface){
                    NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' declares interface '{}' but mesh shader '{}' "
                        "does not include a generated material bind")
                        , StringConvert(materialEntry.virtualPath.c_str())
                        , StringConvert(materialEntry.materialInterface.c_str())
                        , StringConvert(shaderAsset.name().c_str())
                    );
                    return false;
                }
                if(shaderMaterialInterface != materialEntry.materialInterface){
                    NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' declares interface '{}' but mesh shader '{}' "
                        "includes generated material bind interface '{}'")
                        , StringConvert(materialEntry.virtualPath.c_str())
                        , StringConvert(materialEntry.materialInterface.c_str())
                        , StringConvert(shaderAsset.name().c_str())
                        , StringConvert(shaderMaterialInterfacePath)
                    );
                    return false;
                }
            }
        }

        if(!hasShaderStage){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' has no shader stages")
                , StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }

        if(!hasMeshShader){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' declares interface '{}' but has no mesh shader stage")
                , StringConvert(materialEntry.virtualPath.c_str())
                , StringConvert(materialEntry.materialInterface.c_str())
            );
            return false;
        }
    }

    return true;
}


static VariantCachePaths BuildVariantCachePaths(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView shaderSafeName,
    const AStringView stageSafeName,
    const AStringView variantName,
    ScratchArena& scratchArena
){
    ScratchString bytecodeFileName{scratchArena};
    bytecodeFileName.reserve(shaderSafeName.size() + 2u + stageSafeName.size() + 2u + 16u + 4u);
    bytecodeFileName += shaderSafeName;
    bytecodeFileName += "__";
    bytecodeFileName += stageSafeName;
    bytecodeFileName += "__";
    AppendHexU64(ComputeFnv64Text(variantName), bytecodeFileName);
    bytecodeFileName += ".spv";

    VariantCachePaths cachePaths;
    cachePaths.bytecodePath = cacheDirectory / configurationSafeName / bytecodeFileName;
    cachePaths.sourceChecksumPath = cachePaths.bytecodePath;
    cachePaths.sourceChecksumPath += ".source";
    return cachePaths;
}


static bool GetVariantBytecode(
    const ShaderCook::ShaderEntry& entry,
    const AStringView variantName,
    const ShaderCook::DefineCombo& defineCombo,
    const ShaderCook::CookVector<Path>& includeDirectories,
    const Path& sourcePath,
    const VariantCachePaths& cachePaths,
    const AStringView sourceChecksumHex,
    ShaderCook& shaderCook,
    Core::GraphicsBytes& outBytecode,
    ScratchArena& scratchArena
){
    ErrorCode errorCode;

    outBytecode.clear();

    errorCode.clear();
    const bool hasCachedBytecode = FileExists(cachePaths.bytecodePath, errorCode);
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to query bytecode cache '{}' for entry '{}': {}")
            , PathToString<tchar>(cachePaths.bytecodePath)
            , StringConvert(entry.name)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    errorCode.clear();
    const bool hasCachedChecksum = FileExists(cachePaths.sourceChecksumPath, errorCode);
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to query checksum cache '{}' for entry '{}': {}")
            , PathToString<tchar>(cachePaths.sourceChecksumPath)
            , StringConvert(entry.name)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    bool cacheUpToDate = false;
    if(hasCachedBytecode && hasCachedChecksum){
        ScratchString cachedText{scratchArena};
        cacheUpToDate = ReadTextFile(cachePaths.sourceChecksumPath, cachedText)
            && TrimView(cachedText) == AStringView(sourceChecksumHex.data(), sourceChecksumHex.size())
        ;
    }
    if(cacheUpToDate){
        errorCode.clear();
        if(ReadBinaryFile(cachePaths.bytecodePath, outBytecode, errorCode) && !outBytecode.empty() && (outBytecode.size() & 3u) == 0u)
            return true;
        outBytecode.clear();
    }

    HashMap<
        AStringView,
        AStringView,
        Hasher<AStringView>,
        EqualTo<AStringView>,
        ScratchArena
    > mergedDefines(
        0,
        Hasher<AStringView>(),
        EqualTo<AStringView>(),
        scratchArena
    );
    if(defineCombo.size() > Limit<usize>::s_Max - entry.implicitDefines.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: define count overflow for entry '{}'"), StringConvert(entry.name));
        return false;
    }

    const usize mergedDefineCapacity = defineCombo.size() + entry.implicitDefines.size();
    mergedDefines.reserve(mergedDefineCapacity);
    for(const auto& [defineName, value] : defineCombo)
        mergedDefines.insert_or_assign(AStringView(defineName), AStringView(value));
    for(const auto& [defineName, value] : entry.implicitDefines)
        mergedDefines.insert_or_assign(AStringView(defineName), AStringView(value));

    Vector<ShaderCook::ShaderMacroDefinition, ScratchArena> compileDefines{ scratchArena };
    if(mergedDefines.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: entry '{}' has too many merged defines for shader compilation")
            , StringConvert(entry.name)
        );
        return false;
    }
    compileDefines.reserve(mergedDefines.size());
    for(const auto& [defineName, value] : mergedDefines)
        compileDefines.push_back(ShaderCook::ShaderMacroDefinition{ defineName, value });
    Sort(compileDefines.begin(), compileDefines.end(), [](const ShaderCook::ShaderMacroDefinition& lhs, const ShaderCook::ShaderMacroDefinition& rhs){
        return lhs.name < rhs.name;
    });

    errorCode.clear();
    if(!EnsureDirectories(cachePaths.bytecodePath.parent_path(), errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to create cache directory '{}': {}")
            , PathToString<tchar>(cachePaths.bytecodePath.parent_path())
            , StringConvert(errorCode.message())
        );
        return false;
    }

    const ShaderCook::ShaderCompilerRequest compileRequest = {
        entry.name,
        entry.stage.view(),
        entry.targetProfile.view(),
        entry.entryPoint,
        variantName,
        compileDefines.data(),
        static_cast<u32>(compileDefines.size()),
        includeDirectories,
        sourcePath,
        cachePaths.bytecodePath
    };
    if(!shaderCook.compileVariant(compileRequest, outBytecode))
        return false;

    if(!WriteTextFile(cachePaths.sourceChecksumPath, sourceChecksumHex)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write cook source checksum '{}'")
            , PathToString<tchar>(cachePaths.sourceChecksumPath)
        );
        return false;
    }

    return true;
}

static bool AddPlannedFileCount(const u64 additionalFileCount, u64& inOutPlannedFileCount){
    if(inOutPlannedFileCount > Limit<u64>::s_Max - additionalFileCount){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: planned file count overflow"));
        return false;
    }

    inOutPlannedFileCount += additionalFileCount;
    return true;
}

static bool ReserveShaderIndexRecords(
    const PreparedShaderVector& preparedEntries,
    Core::GraphicsVector<Core::ShaderArchive::Record>& outShaderIndexRecords,
    usize& outShaderRecordCount
){
    outShaderRecordCount = 0u;

    u64 shaderRecordCount = 0;
    for(const PreparedShaderEntry& preparedEntry : preparedEntries){
        if(shaderRecordCount > Limit<u64>::s_Max - preparedEntry.variantCount){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: shader record count overflow"));
            return false;
        }
        shaderRecordCount += preparedEntry.variantCount;
    }
    if(shaderRecordCount > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: shader record count exceeds container capacity"));
        return false;
    }

    outShaderIndexRecords.clear();
    outShaderRecordCount = static_cast<usize>(shaderRecordCount);
    outShaderIndexRecords.reserve(outShaderRecordCount);
    return true;
}

[[nodiscard]] static bool MetadataDeclaresSkinnedGeometry(const Core::Metascript::Document& doc){
    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap())
        return false;

    const Core::Metascript::Value* geometryClass = asset.findField("geometry_class");
    if(geometryClass && geometryClass->isString()){
        const Core::Metascript::MStringView text = geometryClass->asString();
        if(AStringView(text.data(), text.size()) == AStringView("skinned"))
            return true;
    }

    return
        asset.findField("skin")
        || asset.findField("skeleton_joint_count")
        || asset.findField("inverse_bind_matrices")
    ;
}

static bool ParseAssetMetadata(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const DiscoveredNwbFileVector& nwbFiles,
    const DiscoveredBindFileVector& bindFiles,
    ParsedAssetMetadata& outMetadata,
    ScratchArena& scratchArena
){
    outMetadata.includeMetadata.reserve(nwbFiles.size());
    outMetadata.shaderEntries.reserve(nwbFiles.size());
    outMetadata.materialBindEntries.reserve(bindFiles.size());
    outMetadata.materialEntries.reserve(nwbFiles.size());
    outMetadata.geometryEntries.reserve(nwbFiles.size());
    outMetadata.skinnedGeometryEntries.reserve(nwbFiles.size());

    HashSet<
        PreparedShaderKey,
        PreparedShaderKeyHasher,
        EqualTo<PreparedShaderKey>,
        ShaderCook::CookArena
    > seenShaderIdentityKeys{
        0,
        PreparedShaderKeyHasher(),
        EqualTo<PreparedShaderKey>(),
        cookArena
    };
    HashSet<NameHash, Hasher<NameHash>, EqualTo<NameHash>, ScratchArena> seenPropertyAssetPathHashes(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        scratchArena
    );

    seenShaderIdentityKeys.reserve(nwbFiles.size());
    seenPropertyAssetPathHashes.reserve(nwbFiles.size());

    for(const DiscoveredNwbFile& discoveredNwbFile : nwbFiles){
        const Path& nwbFile = discoveredNwbFile.filePath;
        Core::Metascript::Document doc(cookArena);
        if(!shaderCook.parseDocument(nwbFile, doc))
            return false;

        const AStringView rawAssetTypeText(doc.assetType().data(), doc.assetType().size());
        const Name assetType = ToName(rawAssetTypeText);
        if(assetType == Shader::AssetTypeName()){
            ShaderCook::ShaderEntry shaderEntry(cookArena);
            if(!shaderCook.parseShaderMeta(nwbFile, doc, shaderEntry, scratchArena))
                return false;

            if(!Core::Assets::BuildDerivedAssetVirtualPath(
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                Path(shaderEntry.source),
                shaderEntry.name
            ))
                return false;

            const PreparedShaderKey shaderIdentityKey{
                ToName(shaderEntry.name),
                ToName(shaderEntry.archiveStage.view())
            };
            if(!seenShaderIdentityKeys.insert(shaderIdentityKey).second){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: duplicate shader identity '{}' for stage '{}' from meta '{}'")
                    , StringConvert(shaderEntry.name)
                    , StringConvert(shaderEntry.archiveStage.c_str())
                    , PathToString<tchar>(nwbFile)
                );
                return false;
            }

            if(!shaderEntry.name.empty())
                outMetadata.shaderEntries.push_back(Move(shaderEntry));
            continue;
        }

        if(assetType == IncludeAssetTypeName()){
            ShaderCook::IncludeEntry includeEntry(cookArena);
            if(!shaderCook.parseIncludeMeta(nwbFile, doc, includeEntry, scratchArena))
                return false;

            if(!includeEntry.source.empty() && !includeEntry.defineValues.empty()){
                ErrorCode errorCode;
                const Path absSource = AbsolutePath(Path(includeEntry.source), errorCode).lexically_normal();
                if(errorCode){
                    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to resolve include metadata source '{}' from '{}': {}")
                        , StringConvert(includeEntry.source)
                        , PathToString<tchar>(nwbFile)
                        , StringConvert(errorCode.message())
                    );
                    return false;
                }

                ScratchString key = PathToString(scratchArena, absSource);
                CanonicalizeTextInPlace(key);
                CookString cookKey(key, cookArena);
                if(!outMetadata.includeMetadata.emplace(Move(cookKey), Move(includeEntry)).second){
                    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: duplicate include metadata for source '{}'")
                        , PathToString<tchar>(absSource)
                    );
                    return false;
                }
            }

            continue;
        }

        if(assetType == Material::AssetTypeName()){
            MaterialCookEntry materialEntry(cookArena);
            if(!ParseMaterialCookMetadata(
                shaderCook,
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                discoveredNwbFile.filePath,
                doc,
                materialEntry,
                scratchArena
            ))
                return false;

            if(!AppendUniquePropertyAssetEntry(materialEntry, seenPropertyAssetPathHashes, outMetadata.materialEntries))
                return false;
            continue;
        }

        if(assetType == Geometry::AssetTypeName() && MetadataDeclaresSkinnedGeometry(doc)){
            SkinnedGeometryCookEntry geometryEntry(cookArena);
            if(!ParseSkinnedGeometryCookMetadata(
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                discoveredNwbFile.filePath,
                doc,
                geometryEntry,
                scratchArena
            ))
                return false;

            if(!AppendUniquePropertyAssetEntry(geometryEntry, seenPropertyAssetPathHashes, outMetadata.skinnedGeometryEntries))
                return false;
            continue;
        }

        if(assetType == Geometry::AssetTypeName()){
            GeometryCookEntry geometryEntry(cookArena);
            if(!ParseGeometryCookMetadata(
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                discoveredNwbFile.filePath,
                doc,
                geometryEntry,
                scratchArena
            ))
                return false;

            if(!AppendUniquePropertyAssetEntry(geometryEntry, seenPropertyAssetPathHashes, outMetadata.geometryEntries))
                return false;
            continue;
        }

        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: unsupported asset type '{}' in meta '{}'")
            , StringConvert(rawAssetTypeText)
            , PathToString<tchar>(nwbFile)
        );
        return false;
    }

    for(const DiscoveredNwbFile& discoveredBindFile : bindFiles){
        MaterialBindEntry bindEntry(cookArena);
        if(!ParseMaterialBindSource(discoveredBindFile.filePath, bindEntry, scratchArena))
            return false;

        if(!Core::Assets::BuildDerivedAssetVirtualPath(
            discoveredBindFile.assetRoot,
            discoveredBindFile.virtualRoot.view(),
            discoveredBindFile.filePath,
            bindEntry.virtualPath
        ))
            return false;

        outMetadata.materialBindEntries.push_back(Move(bindEntry));
    }

    if(outMetadata.shaderEntries.empty()){
        if(!outMetadata.materialEntries.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: material assets require at least one shader entry"));
            return false;
        }
        if(!outMetadata.geometryEntries.empty())
            return true;
        if(!outMetadata.skinnedGeometryEntries.empty())
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: no graphics asset metadata found in asset roots"));
        return false;
    }

    return true;
}

static bool PrepareShaderEntriesForCook(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const ResolvedCookPaths& resolvedPaths,
    const Path& materialBindIncludeRoot,
    const IncludeMetadataMap& includeMetadata,
    ShaderEntryVector& inOutShaderEntries,
    PreparedShaderPlan& outPreparedPlan,
    ScratchArena& scratchArena
){
    ErrorCode errorCode;

    outPreparedPlan.preparedEntries.clear();
    if(inOutShaderEntries.size() > Limit<usize>::s_Max / 2u){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: prepared shader entry reserve count overflows"));
        return false;
    }
    outPreparedPlan.preparedEntries.reserve(inOutShaderEntries.size() * 2u);
    outPreparedPlan.plannedFileCount = 1; // shader archive index

    for(ShaderCook::ShaderEntry& entry : inOutShaderEntries){
        PreparedShaderEntry preparedEntry(cookArena);
        preparedEntry.entry = Move(entry);

        errorCode.clear();
        if(!ResolveAbsolutePath(resolvedPaths.repoRoot, preparedEntry.entry.source, preparedEntry.sourcePath, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve source path '{}' for entry '{}': {}")
                    , StringConvert(preparedEntry.entry.source)
                    , StringConvert(preparedEntry.entry.name)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve source path '{}' for entry '{}': path is empty or invalid")
                    , StringConvert(preparedEntry.entry.source)
                    , StringConvert(preparedEntry.entry.name)
                );
            }
            return false;
        }

        errorCode.clear();
        const bool sourceExists = FileExists(preparedEntry.sourcePath, errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query source path '{}' for entry '{}': {}")
                , PathToString<tchar>(preparedEntry.sourcePath)
                , StringConvert(preparedEntry.entry.name)
                , StringConvert(errorCode.message())
            );
            return false;
        }
        if(!sourceExists){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader source does not exist for entry '{}': '{}'")
                , StringConvert(preparedEntry.entry.name)
                , PathToString<tchar>(preparedEntry.sourcePath)
            );
            return false;
        }

        errorCode.clear();
        const bool isRegularSourceFile = IsRegularFile(preparedEntry.sourcePath, errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to inspect source path '{}' for entry '{}': {}")
                , PathToString<tchar>(preparedEntry.sourcePath)
                , StringConvert(preparedEntry.entry.name)
                , StringConvert(errorCode.message())
            );
            return false;
        }
        if(!isRegularSourceFile){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader source is not a regular file for entry '{}': '{}'")
                , StringConvert(preparedEntry.entry.name)
                , PathToString<tchar>(preparedEntry.sourcePath)
            );
            return false;
        }

        if(!BuildIncludeDirectories(
            resolvedPaths.repoRoot,
            resolvedPaths.assetRoots,
            materialBindIncludeRoot,
            preparedEntry.entry,
            preparedEntry.includeDirectories,
            scratchArena
        ))
            return false;
        if(!shaderCook.gatherShaderDependencies(
            preparedEntry.sourcePath,
            preparedEntry.includeDirectories,
            preparedEntry.dependencies,
            scratchArena
        ))
            return false;

        shaderCook.mergeInheritedDefines(preparedEntry.entry, preparedEntry.dependencies, includeMetadata);
        if(!ValidateShaderDoesNotUseImplicitDefine(preparedEntry.entry, MaterialBindNames::TypedBindingImplicitDefineText()))
            return false;

        CookString materialTypedBindingInterfaceText{cookArena};
        if(!ResolveMaterialBindDependencyInterface(
            AStringView(preparedEntry.entry.name),
            materialBindIncludeRoot,
            preparedEntry.dependencies,
            materialTypedBindingInterfaceText,
            preparedEntry.materialTypedBindingInterface,
            scratchArena
        ))
            return false;
        preparedEntry.usesMaterialTypedBinding =
            preparedEntry.entry.archiveStage.view() == "mesh"
            && preparedEntry.materialTypedBindingInterface
        ;
        preparedEntry.materialTypedBindingInterfacePath = Move(materialTypedBindingInterfaceText);
        if(preparedEntry.usesMaterialTypedBinding && !SetShaderImplicitDefine(
            preparedEntry.entry,
            MaterialBindNames::TypedBindingImplicitDefineText(),
            MaterialBindNames::TypedBindingImplicitDefineValueText()
        ))
            return false;
        if(!shaderCook.computeDependencyChecksum(preparedEntry.dependencies, preparedEntry.dependencyChecksum, scratchArena))
            return false;
        if(!CountShaderVariants(preparedEntry.entry, preparedEntry.variantCount))
            return false;
        if(!AddPlannedFileCount(preparedEntry.variantCount, outPreparedPlan.plannedFileCount))
            return false;

        const bool emitMeshComputeShadow = preparedEntry.entry.archiveStage.view() == "mesh";
        outPreparedPlan.preparedEntries.push_back(Move(preparedEntry));

        if(!emitMeshComputeShadow)
            continue;

        const PreparedShaderEntry& meshShaderEntry = outPreparedPlan.preparedEntries.back();
        PreparedShaderEntry meshComputeShadowEntry(cookArena);
        if(!BuildMeshComputeShadowEntry(meshShaderEntry.entry, meshComputeShadowEntry.entry)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to build mesh-compute shadow entry for '{}'")
                , StringConvert(meshShaderEntry.entry.name)
            );
            return false;
        }

        meshComputeShadowEntry.sourcePath = meshShaderEntry.sourcePath;
        meshComputeShadowEntry.includeDirectories = meshShaderEntry.includeDirectories;
        meshComputeShadowEntry.dependencies = meshShaderEntry.dependencies;
        meshComputeShadowEntry.dependencyChecksum = meshShaderEntry.dependencyChecksum;
        meshComputeShadowEntry.variantCount = meshShaderEntry.variantCount;
        meshComputeShadowEntry.materialTypedBindingInterfacePath = meshShaderEntry.materialTypedBindingInterfacePath;
        meshComputeShadowEntry.materialTypedBindingInterface = meshShaderEntry.materialTypedBindingInterface;
        meshComputeShadowEntry.usesMaterialTypedBinding = meshShaderEntry.usesMaterialTypedBinding;

        if(!AddPlannedFileCount(meshComputeShadowEntry.variantCount, outPreparedPlan.plannedFileCount))
            return false;

        outPreparedPlan.preparedEntries.push_back(Move(meshComputeShadowEntry));
    }

    return true;
}

static bool AppendPreparedShadersToVolume(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    PreparedShaderVector& preparedEntries,
    Core::Filesystem::VolumeSession& volumeSession,
    VirtualPathHashSet& inOutSeenVirtualPathHashes,
    const usize shaderRecordCount,
    Core::GraphicsVector<Core::ShaderArchive::Record>& outShaderIndexRecords,
    ScratchArena& scratchArena
){
    Core::GraphicsBytes cookedBytecode{cookArena};
    ShaderCook::CookVector<ShaderCook::DefineCombo> defineCombinations{ cookArena };
    outShaderIndexRecords.clear();

    for(PreparedShaderEntry& preparedEntry : preparedEntries){
        ShaderCook::ShaderEntry& entry = preparedEntry.entry;
        const Name shaderName = ToName(entry.name);
        const Name stageName = ToName(entry.archiveStage.view());
        if(!shaderName || !stageName || entry.entryPoint.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader cook failed to canonicalize shader identity for '{}' stage '{}' entry point '{}'")
                , StringConvert(entry.name)
                , StringConvert(entry.archiveStage.c_str())
                , StringConvert(entry.entryPoint)
            );
            return false;
        }

        const CookString shaderSafeName = BuildSafeCacheName(entry.name);
        const CookString stageSafeName = BuildSafeCacheName(cookArena, entry.archiveStage.view());

        if(!shaderCook.expandDefineCombinations(entry.defineValues, defineCombinations, scratchArena)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: variant combination count exceeds runtime limits for entry '{}'")
                , StringConvert(entry.name)
            );
            return false;
        }
        if(defineCombinations.empty())
            defineCombinations.push_back(ShaderCook::DefineCombo(cookArena));

        for(const ShaderCook::DefineCombo& defineCombo : defineCombinations){
            const CookString generatedVariantName = shaderCook.buildVariantName(defineCombo, scratchArena);

            u64 sourceChecksum = 0;
            if(!shaderCook.computeSourceChecksum(
                entry,
                generatedVariantName,
                preparedEntry.dependencyChecksum,
                sourceChecksum,
                scratchArena
            ))
                return false;
            const CookString sourceChecksumHex = FormatHex64(cookArena, sourceChecksum);

            const VariantCachePaths cachePaths = BuildVariantCachePaths(
                cacheDirectory,
                configurationSafeName,
                shaderSafeName,
                stageSafeName,
                generatedVariantName,
                scratchArena
            );
            if(!GetVariantBytecode(
                entry,
                generatedVariantName,
                defineCombo,
                preparedEntry.includeDirectories,
                preparedEntry.sourcePath,
                cachePaths,
                sourceChecksumHex,
                shaderCook,
                cookedBytecode,
                scratchArena
            ))
                return false;

            const Name virtualPath = Core::ShaderArchive::buildVirtualPathName(shaderName, generatedVariantName, stageName);
            if(!virtualPath){
                NWB_LOGGER_ERROR(NWB_TEXT("Shader cook failed to build virtual path for '{}' stage '{}' variant '{}'")
                    , StringConvert(entry.name)
                    , StringConvert(entry.archiveStage.c_str())
                    , StringConvert(generatedVariantName)
                );
                return false;
            }

            const NameHash virtualPathHash = virtualPath.hash();
            if(!inOutSeenVirtualPathHashes.insert(virtualPathHash).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Shader cook produced duplicate virtual path '{}' (entry='{}', variant='{}')")
                    , StringConvert(virtualPath.c_str())
                    , StringConvert(entry.name)
                    , StringConvert(generatedVariantName)
                );
                return false;
            }

            if(!volumeSession.pushDataDeferred(virtualPath, cookedBytecode)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to push shader bytecode '{}'"), StringConvert(virtualPath.c_str()));
                return false;
            }

            Core::ShaderArchive::Record record(cookArena);
            record.shaderName = shaderName;
            record.variantName = generatedVariantName;
            record.stage = stageName;
            record.sourceChecksum = sourceChecksum;
            record.bytecodeChecksum = ComputeFnv64Bytes(cookedBytecode.data(), cookedBytecode.size());
            record.virtualPathHash = virtualPathHash;
            if(outShaderIndexRecords.size() >= shaderRecordCount){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: shader record count exceeded prepared capacity"));
                return false;
            }
            outShaderIndexRecords.push_back(Move(record));
        }
    }

    if(outShaderIndexRecords.size() != shaderRecordCount){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: shader record count mismatch after cook"));
        return false;
    }
    return true;
}

static bool RegisterVolumeAssetPath(
    const tchar* assetKind,
    const Name& virtualPath,
    VirtualPathHashSet& inOutSeenVirtualPathHashes
){
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: invalid {} virtual path"), assetKind);
        return false;
    }

    const NameHash virtualPathHash = virtualPath.hash();
    if(!inOutSeenVirtualPathHashes.insert(virtualPathHash).second){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: duplicate {} virtual path '{}'")
            , assetKind
            , StringConvert(virtualPath.c_str())
        );
        return false;
    }

    return true;
}

static bool PushSerializedAssetToVolume(
    const tchar* assetKind,
    const Name& virtualPath,
    const Core::Assets::IAsset& asset,
    const Core::Assets::IAssetCodec& codec,
    Core::Filesystem::VolumeSession& volumeSession,
    Core::Assets::AssetBytes& scratchBinary
){
    scratchBinary.clear();
    if(!codec.serialize(asset, scratchBinary)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to serialize {} '{}'")
            , assetKind
            , StringConvert(virtualPath.c_str())
        );
        return false;
    }

    if(!volumeSession.pushDataDeferred(virtualPath, scratchBinary)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to push {} '{}'")
            , assetKind
            , StringConvert(virtualPath.c_str())
        );
        return false;
    }

    return true;
}

template<typename AssetT, typename AssetCodecT, typename EntryVectorT, typename BuildAssetFn>
static bool AppendBuiltAssetsToVolume(
    const tchar* assetKind,
    EntryVectorT& assetEntries,
    Core::Filesystem::VolumeSession& volumeSession,
    VirtualPathHashSet& inOutSeenVirtualPathHashes,
    const bool logBuildFailure,
    BuildAssetFn&& buildAsset
){
    AssetCodecT assetCodec;
    Core::Assets::AssetArena& assetArena = assetEntries.get_allocator().arena();
    Core::Assets::AssetBytes assetBinary{assetArena};

    for(auto& assetEntry : assetEntries){
        if(!RegisterVolumeAssetPath(assetKind, assetEntry.virtualPath, inOutSeenVirtualPathHashes))
            return false;

        AssetT cookedAsset(assetArena);
        if(!buildAsset(assetEntry, cookedAsset)){
            if(logBuildFailure){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to build {} '{}'")
                    , assetKind
                    , StringConvert(assetEntry.virtualPath.c_str())
                );
            }
            return false;
        }

        if(!PushSerializedAssetToVolume(
            assetKind,
            assetEntry.virtualPath,
            cookedAsset,
            assetCodec,
            volumeSession,
            assetBinary
        ))
            return false;
    }

    return true;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GraphicsAssetCooker::cook(const Core::Assets::AssetCookOptions& options){
    GraphicsCookEnvironment environment(m_arena);
    environment.configuration = options.configuration;
    environment.repoRoot = options.repoRoot.empty() ? Path(".") : Path(options.repoRoot.c_str());
    environment.assetRoots.reserve(options.assetRoots.size());
    for(const Core::Assets::AssetString& assetRoot : options.assetRoots)
        environment.assetRoots.push_back(Path(assetRoot.c_str()));
    environment.outputDirectory = Path(options.outputDirectory.c_str());
    environment.cacheDirectory = options.cacheDirectory.empty() ? Path() : Path(options.cacheDirectory.c_str());

    GraphicsCookResult result;
    if(!cookGraphicsAssets(environment, result))
        return false;

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Graphics asset cook complete [{}] - volume='{}', files={}, segments={}, mount='{}'"),
        StringConvert(options.configuration.c_str()),
        StringConvert(result.volumeName.c_str()),
        result.fileCount,
        result.segmentCount,
        PathToString<tchar>(environment.outputDirectory)
    );

    return true;
}


bool GraphicsAssetCooker::cookGraphicsAssets(const GraphicsCookEnvironment& environment, GraphicsCookResult& outResult){
    outResult = {};

    ShaderCook shaderCook(m_arena);
    Core::Alloc::ScratchArena scratchArena;

    __hidden_graphics_asset_cooker::ResolvedCookPaths resolvedPaths(m_arena);
    if(!__hidden_graphics_asset_cooker::ResolveCookPaths(environment, resolvedPaths, scratchArena))
        return false;

    // discover cook input files from asset roots
    __hidden_graphics_asset_cooker::DiscoveredNwbFileVector nwbFiles{ m_arena };
    if(!__hidden_graphics_asset_cooker::DiscoverFilesWithExtension(
        resolvedPaths.assetRoots,
        Core::Assets::s_NwbExtension,
        nwbFiles,
        scratchArena
    ))
        return false;

    __hidden_graphics_asset_cooker::DiscoveredBindFileVector bindFiles{ m_arena };
    if(!__hidden_graphics_asset_cooker::DiscoverFilesWithExtension(
        resolvedPaths.assetRoots,
        MaterialBindNames::SourceExtensionText(),
        bindFiles,
        scratchArena
    ))
        return false;

    __hidden_graphics_asset_cooker::ParsedAssetMetadata parsedMetadata(m_arena);
    if(!__hidden_graphics_asset_cooker::ParseAssetMetadata(m_arena, shaderCook, nwbFiles, bindFiles, parsedMetadata, scratchArena))
        return false;
    if(!ValidateMaterialCookInterfaces(parsedMetadata.materialBindEntries, parsedMetadata.materialEntries, scratchArena))
        return false;

    __hidden_graphics_asset_cooker::CookString configurationSafeName = BuildCanonicalSafeCacheName(m_arena, environment.configuration.view());
    if(configurationSafeName.empty())
        configurationSafeName = "default";

    Path materialBindIncludeRoot;
    if(!EmitMaterialBindIncludes(
        m_arena,
        resolvedPaths.cacheDirectory,
        configurationSafeName,
        parsedMetadata.materialBindEntries,
        materialBindIncludeRoot,
        scratchArena
    ))
        return false;

    __hidden_graphics_asset_cooker::PreparedShaderPlan preparedPlan(m_arena);
    if(!__hidden_graphics_asset_cooker::PrepareShaderEntriesForCook(
        m_arena,
        shaderCook,
        resolvedPaths,
        materialBindIncludeRoot,
        parsedMetadata.includeMetadata,
        parsedMetadata.shaderEntries,
        preparedPlan,
        scratchArena
    ))
        return false;
    if(!__hidden_graphics_asset_cooker::AddPlannedFileCount(static_cast<u64>(parsedMetadata.materialEntries.size()), preparedPlan.plannedFileCount))
        return false;
    if(!__hidden_graphics_asset_cooker::AddPlannedFileCount(static_cast<u64>(parsedMetadata.geometryEntries.size()), preparedPlan.plannedFileCount))
        return false;
    if(!__hidden_graphics_asset_cooker::AddPlannedFileCount(
        static_cast<u64>(parsedMetadata.skinnedGeometryEntries.size()),
        preparedPlan.plannedFileCount
    ))
        return false;
    if(!__hidden_graphics_asset_cooker::ValidateMaterials(
        shaderCook,
        preparedPlan.preparedEntries,
        parsedMetadata.materialEntries,
        scratchArena
    ))
        return false;

    Core::GraphicsVector<Core::ShaderArchive::Record> shaderIndexRecords{ m_arena };
    usize shaderIndexRecordCount = 0u;
    if(!__hidden_graphics_asset_cooker::ReserveShaderIndexRecords(preparedPlan.preparedEntries, shaderIndexRecords, shaderIndexRecordCount))
        return false;

    __hidden_graphics_asset_cooker::VirtualPathHashSet seenVirtualPathHashes{m_arena};
    if(preparedPlan.plannedFileCount <= static_cast<u64>(Limit<usize>::s_Max))
        seenVirtualPathHashes.reserve(static_cast<usize>(preparedPlan.plannedFileCount));
    const Name& shaderIndexVirtualPath = Core::ShaderArchive::IndexVirtualPathName();
    seenVirtualPathHashes.insert(shaderIndexVirtualPath.hash());

    Core::Filesystem::VolumeBuildConfig volumeConfig;
    if(!__hidden_graphics_asset_cooker::ConfigureVolumeSizing(preparedPlan.plannedFileCount, volumeConfig))
        return false;

    const __hidden_graphics_asset_cooker::StagedVolumePaths stagedVolumePaths = __hidden_graphics_asset_cooker::BuildStagedVolumePaths(
        resolvedPaths.outputDirectory,
        volumeConfig.volumeName,
        configurationSafeName,
        scratchArena
    );
    if(!Core::Filesystem::EnsureEmptyStagedDirectory(stagedVolumePaths.stageDirectory, __hidden_graphics_asset_cooker::s_CookerLogPrefix, "stage directory"))
        return false;
    Core::Filesystem::StagedDirectoryCleanupGuard stageDirectoryCleanup(stagedVolumePaths.stageDirectory, __hidden_graphics_asset_cooker::s_CookerLogPrefix);
    if(!Core::Filesystem::RemoveStagedDirectoryIfPresent(stagedVolumePaths.backupDirectory, __hidden_graphics_asset_cooker::s_CookerLogPrefix, "backup directory"))
        return false;

    u64 stagedFileCount = 0;
    usize stagedSegmentCount = 0;
    {
        Core::Filesystem::VolumeSession volumeSession(m_arena);
        if(!volumeSession.create(stagedVolumePaths.stageDirectory, volumeConfig)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to create staged volume session"));
            return false;
        }

        if(!__hidden_graphics_asset_cooker::AppendPreparedShadersToVolume(
            m_arena,
            shaderCook,
            resolvedPaths.cacheDirectory,
            configurationSafeName,
            preparedPlan.preparedEntries,
            volumeSession,
            seenVirtualPathHashes,
            shaderIndexRecordCount,
            shaderIndexRecords,
            scratchArena
        ))
            return false;

        Core::GraphicsBytes indexBinary{m_arena};
        if(!Core::ShaderArchive::serializeIndex(shaderIndexRecords, indexBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to serialize shader index"));
            return false;
        }
        if(!volumeSession.pushDataDeferred(shaderIndexVirtualPath, indexBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to push shader index"));
            return false;
        }

        if(!__hidden_graphics_asset_cooker::AppendBuiltAssetsToVolume<Material, MaterialAssetCodec>(
            NWB_TEXT("material"),
            parsedMetadata.materialEntries,
            volumeSession,
            seenVirtualPathHashes,
            false,
            [](const MaterialCookEntry& materialEntry, Material& outMaterial){
                return BuildMaterialAsset(materialEntry, outMaterial);
            }
        ))
            return false;
        if(!__hidden_graphics_asset_cooker::AppendBuiltAssetsToVolume<Geometry, GeometryAssetCodec>(
            NWB_TEXT("geometry"),
            parsedMetadata.geometryEntries,
            volumeSession,
            seenVirtualPathHashes,
            true,
            [](GeometryCookEntry& geometryEntry, Geometry& outGeometry){
                return BuildGeometryAsset(geometryEntry, outGeometry);
            }
        ))
            return false;
        if(!__hidden_graphics_asset_cooker::AppendBuiltAssetsToVolume<SkinnedGeometry, SkinnedGeometryAssetCodec>(
            NWB_TEXT("skinned geometry"),
            parsedMetadata.skinnedGeometryEntries,
            volumeSession,
            seenVirtualPathHashes,
            true,
            [](SkinnedGeometryCookEntry& geometryEntry, SkinnedGeometry& outGeometry){
                return BuildSkinnedGeometryAsset(geometryEntry, outGeometry);
            }
        ))
            return false;
        if(!volumeSession.flush()){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to flush staged volume metadata"));
            return false;
        }

        stagedFileCount = volumeSession.fileCount();
        stagedSegmentCount = volumeSession.segmentCount();
    }

    if(!Core::Filesystem::PublishStagedVolume(stagedVolumePaths, resolvedPaths.outputDirectory, volumeConfig.volumeName, stagedSegmentCount))
        return false;
    stageDirectoryCleanup.dismiss();

    if(!outResult.volumeName.assign(__hidden_graphics_asset_cooker::s_VolumeName)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: volume name exceeds ACompactString capacity"));
        return false;
    }
    outResult.fileCount = stagedFileCount;
    outResult.segmentCount = static_cast<u64>(stagedSegmentCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

