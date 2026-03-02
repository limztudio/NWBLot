// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_asset_cooker.h"

#if defined(NWB_COOK)
#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_compiler.h>
#include <core/graphics/shader_cook.h>

#include <core/filesystem/filesystem.h>
#include <core/alloc/core.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_DefaultVariant = "default";


static AString NormalizeCookConfiguration(const AStringView configuration){
    if(configuration.empty())
        return "default";

    const AString normalized = CanonicalizeText(configuration);
    if(normalized.empty())
        return "default";
    return normalized;
}


static AString NormalizeVariantName(
    const Core::ShaderCook::ManifestEntry& entry,
    const AStringView generatedVariantName
){
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
    Path manifestPath;
    Path outputDirectory;
    Path cacheDirectory;
};

struct VariantCachePaths{
    Path bytecodePath;
    Path sourceChecksumPath;
};


static bool WriteCacheSourceChecksum(
    const Path& checksumPath,
    AStringView sourceChecksumHex,
    AString& outError
);
static bool CacheSourceChecksumMatches(const Path& checksumPath, AStringView sourceChecksumHex);


static bool ResolvePathFromRepoRoot(
    const Path& repoRoot,
    const Path& inputPath,
    const AStringView pathLabel,
    Path& outPath,
    AString& outError
){
    if(ResolveAbsolutePath(repoRoot, PathToString(inputPath), outPath))
        return true;

    outError = StringFormat(
        "ShaderAssetCooker::CookShaderAssets failed to resolve {} '{}'",
        pathLabel,
        PathToString(inputPath)
    );
    return false;
}


static bool ResolveCookPaths(
    const ShaderCookEnvironment& environment,
    ResolvedCookPaths& outPaths,
    AString& outError
){
    outPaths = {};

    if(environment.manifestPath.empty()){
        outError = "ShaderAssetCooker::CookShaderAssets failed: manifest path is empty";
        return false;
    }
    if(environment.outputDirectory.empty()){
        outError = "ShaderAssetCooker::CookShaderAssets failed: output directory is empty";
        return false;
    }

    ErrorCode errorCode;
    outPaths.repoRoot = environment.repoRoot.empty() ? Path(".") : environment.repoRoot;
    outPaths.repoRoot = AbsolutePath(outPaths.repoRoot, errorCode).lexically_normal();
    if(errorCode){
        outError = StringFormat(
            "ShaderAssetCooker::CookShaderAssets failed to resolve repo root: {}",
            errorCode.message()
        );
        return false;
    }

    if(!ResolvePathFromRepoRoot(outPaths.repoRoot, environment.manifestPath, "manifest path", outPaths.manifestPath, outError))
        return false;
    if(!ResolvePathFromRepoRoot(outPaths.repoRoot, environment.outputDirectory, "output directory", outPaths.outputDirectory, outError))
        return false;

    const Path defaultCacheDirectory = outPaths.repoRoot / "__build_obj/shader_cache";
    const Path requestedCacheDirectory = environment.cacheDirectory.empty()
        ? defaultCacheDirectory
        : environment.cacheDirectory;
    if(!ResolvePathFromRepoRoot(outPaths.repoRoot, requestedCacheDirectory, "cache directory", outPaths.cacheDirectory, outError))
        return false;

    errorCode.clear();
    CreateDirectories(outPaths.cacheDirectory, errorCode);
    if(errorCode){
        outError = StringFormat(
            "ShaderAssetCooker::CookShaderAssets failed to create cache directory '{}': {}",
            PathToString(outPaths.cacheDirectory),
            errorCode.message()
        );
        return false;
    }

    return true;
}


static bool BuildIncludeDirectories(
    const Path& repoRoot,
    const Core::ShaderCook::ManifestData& manifest,
    Core::ShaderCook::CookVector<Path>& outIncludeDirectories,
    AString& outError
){
    outIncludeDirectories.clear();
    outIncludeDirectories.reserve(manifest.includeRoots.size());

    for(const AString& includeRoot : manifest.includeRoots){
        Path includeDirectory;
        if(!ResolveAbsolutePath(repoRoot, includeRoot, includeDirectory)){
            outError = StringFormat("Failed to resolve include_root '{}'", includeRoot);
            return false;
        }

        outIncludeDirectories.push_back(Move(includeDirectory));
    }

    return true;
}


static bool EnsureSourceFileExists(const Path& sourcePath){
    ErrorCode errorCode;
    const bool exists = FileExists(sourcePath, errorCode);
    return exists && !errorCode;
}


static VariantCachePaths BuildVariantCachePaths(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const Core::ShaderCook::ManifestEntry& entry,
    const AStringView variantName
){
    const AString shaderSafeName = BuildSafeCacheName(entry.name);
    const AString variantHashHex = FormatHex64(ComputeFnv64Text(variantName));

    VariantCachePaths cachePaths;
    cachePaths.bytecodePath = cacheDirectory / configurationSafeName / StringFormat(
        "{}__{}__{}.spv",
        shaderSafeName,
        entry.stage,
        variantHashHex
    );
    cachePaths.sourceChecksumPath = cachePaths.bytecodePath;
    cachePaths.sourceChecksumPath += ".source";
    return cachePaths;
}


static bool IsExistingFile(const Path& path){
    ErrorCode errorCode;
    const bool exists = FileExists(path, errorCode);
    return exists && !errorCode;
}


static bool EnsureVariantCache(
    const Core::ShaderCook::ManifestEntry& entry,
    const Core::ShaderCook::DefineCombo& defineCombo,
    const Core::ShaderCook::CookVector<Path>& includeDirectories,
    const Path& sourcePath,
    const VariantCachePaths& cachePaths,
    const AStringView sourceChecksumHex,
    Core::IShaderCompiler& shaderCompiler,
    const bool forceRebuild,
    AString& outError
){
    const bool cacheUpToDate = !forceRebuild
        && IsExistingFile(cachePaths.bytecodePath)
        && IsExistingFile(cachePaths.sourceChecksumPath)
        && CacheSourceChecksumMatches(cachePaths.sourceChecksumPath, sourceChecksumHex);
    if(cacheUpToDate)
        return true;

    const Core::ShaderCompilerRequest compileRequest = {
        entry.name,
        entry.compiler,
        entry.stage,
        entry.targetProfile,
        entry.entryPoint,
        defineCombo,
        includeDirectories,
        sourcePath,
        cachePaths.bytecodePath
    };
    if(!shaderCompiler.compileVariant(compileRequest, outError))
        return false;

    if(!WriteCacheSourceChecksum(cachePaths.sourceChecksumPath, sourceChecksumHex, outError))
        return false;

    return true;
}


static bool LoadBytecodeFromCache(
    const Path& cachePath,
    const AStringView shaderName,
    Core::ShaderCook::CookVector<u8>& outBytecode,
    AString& outError
){
    outBytecode.clear();
    if(!ReadBinaryFile(cachePath, outBytecode)){
        outError = StringFormat(
            "Failed to read shader bytecode cache '{}' for entry '{}'",
            PathToString(cachePath),
            shaderName
        );
        return false;
    }
    if(outBytecode.empty() || (outBytecode.size() & 3u) != 0u){
        outError = StringFormat(
            "Invalid shader bytecode cache '{}' for entry '{}'",
            PathToString(cachePath),
            shaderName
        );
        return false;
    }

    return true;
}


static bool EmitShaderArchiveRecord(
    const Core::ShaderCook::ManifestEntry& entry,
    const AStringView variantName,
    const u64 sourceChecksum,
    const Core::ShaderCook::CookVector<u8>& bytecode,
    Core::ShaderCook::CookHashSet<NameHash>& inOutVirtualPathHashes,
    Core::Filesystem::VolumeSession& volumeSession,
    Vector<Core::ShaderArchive::Record>& inOutRecords,
    AString& outError
){
    const AString variantNameText(variantName);
    const AString virtualPath = Core::ShaderArchive::buildVirtualPath(entry.name, variantName);
    const NameHash virtualPathHash = Name(virtualPath.c_str()).hash();
    if(!inOutVirtualPathHashes.insert(virtualPathHash).second){
        outError = StringFormat(
            "Shader cook produced duplicate virtual path '{}' (entry='{}', variant='{}')",
            virtualPath,
            entry.name,
            variantName
        );
        return false;
    }

    if(!volumeSession.pushData(virtualPath, bytecode, outError))
        return false;

    Core::ShaderArchive::Record record;
    record.shaderName = Name(entry.name.c_str());
    record.variantName = Name(variantNameText.c_str());
    record.stage = Name(entry.stage.c_str());
    record.entryPoint = Name(entry.entryPoint.c_str());
    record.sourceChecksum = sourceChecksum;
    record.bytecodeChecksum = ComputeFnv64Bytes(bytecode.data(), bytecode.size());
    record.virtualPathHash = virtualPathHash;
    inOutRecords.push_back(Move(record));

    return true;
}


static bool WriteCacheSourceChecksum(
    const Path& checksumPath,
    const AStringView sourceChecksumHex,
    AString& outError
){
    Core::ShaderCook::OutputFileStream stream(
        checksumPath,
        Core::ShaderCook::OutputFileStream::binary | Core::ShaderCook::OutputFileStream::trunc
    );
    if(!stream.is_open()){
        outError = StringFormat(
            "Failed to write shader cache checksum '{}'",
            PathToString(checksumPath)
        );
        return false;
    }

    stream.write(
        sourceChecksumHex.data(),
        static_cast<Core::ShaderCook::StreamSize>(sourceChecksumHex.size())
    );
    if(!stream.good()){
        outError = StringFormat(
            "Failed to write shader cache checksum data '{}'",
            PathToString(checksumPath)
        );
        return false;
    }

    return true;
}


static bool CacheSourceChecksumMatches(
    const Path& checksumPath,
    const AStringView sourceChecksumHex
){
    AString cachedChecksumText;
    if(!ReadTextFile(checksumPath, cachedChecksumText))
        return false;

    TrimTrailingCarriageReturn(cachedChecksumText);
    const AString trimmedCachedChecksum = Trim(cachedChecksumText);
    return trimmedCachedChecksum == sourceChecksumHex;
}

static void* VolumeArenaAlloc(usize size){ return Core::Alloc::CoreAlloc(size, "shader_cook_volume"); }
static void VolumeArenaFree(void* ptr){ Core::Alloc::CoreFree(ptr, "shader_cook_volume"); }
static void* VolumeArenaAllocAligned(usize size, usize align){
    return Core::Alloc::CoreAllocAligned(size, align, "shader_cook_volume");
}
static void VolumeArenaFreeAligned(void* ptr){ Core::Alloc::CoreFreeAligned(ptr, "shader_cook_volume"); }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShaderAssetCooker::CookShaderAssets(const ShaderCookEnvironment& environment, ShaderCookResult& outResult, AString& outError){
    outResult = {};
    outError.clear();

    __hidden_assets::ResolvedCookPaths resolvedPaths;
    if(!__hidden_assets::ResolveCookPaths(environment, resolvedPaths, outError))
        return false;

    Core::ShaderCook::ManifestData manifest;
    if(!Core::ShaderCook::ParseManifestFile(resolvedPaths.manifestPath, manifest, outError))
        return false;

    Core::ShaderCook::CookVector<Path> includeDirectories;
    if(!__hidden_assets::BuildIncludeDirectories(resolvedPaths.repoRoot, manifest, includeDirectories, outError))
        return false;

    const AString normalizedConfiguration = __hidden_assets::NormalizeCookConfiguration(environment.configuration);
    const AString configurationSafeName = BuildSafeCacheName(normalizedConfiguration);
    Vector<Core::ShaderArchive::Record> shaderIndexRecords;
    Core::ShaderCook::CookHashSet<NameHash> seenVirtualPathHashes;

    Core::Filesystem::VolumeBuildConfig volumeConfig;
    volumeConfig.volumeName = manifest.volumeName;
    volumeConfig.segmentSize = manifest.segmentSize;
    volumeConfig.metadataSize = manifest.metadataSize;

    Core::Alloc::CustomArena volumeArena(
        __hidden_assets::VolumeArenaAlloc,
        __hidden_assets::VolumeArenaFree,
        __hidden_assets::VolumeArenaAllocAligned,
        __hidden_assets::VolumeArenaFreeAligned
    );

    Core::Filesystem::VolumeSession volumeSession(volumeArena);
    if(!volumeSession.create(resolvedPaths.outputDirectory, volumeConfig, outError))
        return false;

    Core::ShaderCompile shaderCompile;
    Core::IShaderCompiler* const shaderCompiler = shaderCompile.getCompiler();
    if(shaderCompiler == nullptr){
        outError = shaderCompile.error().empty()
            ? AString("Failed to create shader compiler")
            : shaderCompile.error();
        return false;
    }

    for(const Core::ShaderCook::ManifestEntry& entry : manifest.entries){
        Path sourcePath;
        if(!ResolveAbsolutePath(resolvedPaths.repoRoot, entry.source, sourcePath)){
            outError = StringFormat(
                "Failed to resolve source path '{}' for entry '{}'",
                entry.source,
                entry.name
            );
            return false;
        }

        if(!__hidden_assets::EnsureSourceFileExists(sourcePath)){
            outError = StringFormat(
                "Shader source does not exist for entry '{}': '{}'",
                entry.name,
                PathToString(sourcePath)
            );
            return false;
        }

        Core::ShaderCook::CookVector<Path> dependencies;
        if(!Core::ShaderCook::GatherShaderDependencies(sourcePath, includeDirectories, dependencies, outError))
            return false;

        Core::ShaderCook::CookVector<Core::ShaderCook::DefineCombo> defineCombinations;
        Core::ShaderCook::ExpandDefineCombinations(entry.defineValues, defineCombinations);
        if(defineCombinations.empty())
            defineCombinations.push_back({});

        for(const Core::ShaderCook::DefineCombo& defineCombo : defineCombinations){
            const AString generatedVariantName = Core::ShaderCook::BuildVariantName(defineCombo);
            const AString variantName = __hidden_assets::NormalizeVariantName(entry, generatedVariantName);

            u64 sourceChecksum = 0;
            if(!Core::ShaderCook::ComputeSourceChecksum(entry, variantName, dependencies, sourceChecksum, outError))
                return false;
            const AString sourceChecksumHex = FormatHex64(sourceChecksum);

            const __hidden_assets::VariantCachePaths cachePaths = __hidden_assets::BuildVariantCachePaths(
                resolvedPaths.cacheDirectory,
                configurationSafeName,
                entry,
                variantName
            );
            if(!__hidden_assets::EnsureVariantCache(
                entry,
                defineCombo,
                includeDirectories,
                sourcePath,
                cachePaths,
                sourceChecksumHex,
                *shaderCompiler,
                false,
                outError
            )){
                return false;
            }

            Core::ShaderCook::CookVector<u8> cookedBytecode;
            if(!__hidden_assets::LoadBytecodeFromCache(cachePaths.bytecodePath, entry.name, cookedBytecode, outError)){
                if(!__hidden_assets::EnsureVariantCache(
                    entry,
                    defineCombo,
                    includeDirectories,
                    sourcePath,
                    cachePaths,
                    sourceChecksumHex,
                    *shaderCompiler,
                    true,
                    outError
                )){
                    return false;
                }
                if(!__hidden_assets::LoadBytecodeFromCache(cachePaths.bytecodePath, entry.name, cookedBytecode, outError))
                    return false;
            }
            if(!__hidden_assets::EmitShaderArchiveRecord(
                entry,
                variantName,
                sourceChecksum,
                cookedBytecode,
                seenVirtualPathHashes,
                volumeSession,
                shaderIndexRecords,
                outError
            )){
                return false;
            }
        }
    }

    Vector<u8> indexBinary;
    if(!Core::ShaderArchive::serializeIndex(shaderIndexRecords, indexBinary, outError))
        return false;
    if(!volumeSession.pushData(Core::ShaderArchive::s_IndexVirtualPath, indexBinary, outError))
        return false;

    outResult.volumeName = manifest.volumeName;
    outResult.fileCount = volumeSession.fileCount();
    outResult.segmentCount = static_cast<u64>(volumeSession.segmentCount());
    return true;
}


bool ShaderAssetCooker::cook(const Core::Assets::AssetCookOptions& options, AString& outError){
    outError.clear();

    ShaderCookEnvironment environment;
    environment.configuration = options.configuration;
    environment.repoRoot = options.repoRoot.empty() ? Path(".") : Path(options.repoRoot);
    environment.manifestPath = Path(options.manifest);
    environment.outputDirectory = Path(options.outputDirectory);
    environment.cacheDirectory = options.cacheDirectory.empty() ? Path() : Path(options.cacheDirectory);

    ShaderCookResult result;
    if(!CookShaderAssets(environment, result, outError))
        return false;

    NWB_COUT
        << "Shader cook complete ["
        << options.configuration
        << "] - volume='"
        << result.volumeName
        << "', files="
        << result.fileCount
        << ", segments="
        << result.segmentCount
        << ", mount='"
        << environment.outputDirectory.generic_string()
        << "'\n";

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

