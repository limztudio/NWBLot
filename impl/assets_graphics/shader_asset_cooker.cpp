// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_asset_cooker.h"

#if defined(NWB_COOK)
#include <core/assets/asset_cook_utils.h>

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


static AString NormalizeVariantName(const Core::ShaderCook::ManifestEntry& entry, const AStringView generatedVariantName){
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


static bool ResolveCookPaths(const ShaderCookEnvironment& environment, ResolvedCookPaths& outPaths, AString& outError){
    ErrorCode errorCode;

    outPaths = {};

    if(environment.manifestPath.empty()){
        outError = "ShaderAssetCooker::CookShaderAssets failed: manifest path is empty";
        return false;
    }
    if(environment.outputDirectory.empty()){
        outError = "ShaderAssetCooker::CookShaderAssets failed: output directory is empty";
        return false;
    }

    outPaths.repoRoot = environment.repoRoot.empty() ? Path(".") : environment.repoRoot;
    outPaths.repoRoot = AbsolutePath(outPaths.repoRoot, errorCode).lexically_normal();
    if(errorCode){
        outError = StringFormat(
            "ShaderAssetCooker::CookShaderAssets failed to resolve repo root: {}",
            errorCode.message()
        );
        return false;
    }

    if(!Core::Assets::ResolvePathFromCookRoot(outPaths.repoRoot, environment.manifestPath, "manifest path", outPaths.manifestPath, outError))
        return false;
    if(!Core::Assets::ResolvePathFromCookRoot(outPaths.repoRoot, environment.outputDirectory, "output directory", outPaths.outputDirectory, outError))
        return false;

    const Path defaultCacheDirectory = outPaths.repoRoot / "__build_obj/shader_cache";
    const Path requestedCacheDirectory = environment.cacheDirectory.empty()
        ? defaultCacheDirectory
        : environment.cacheDirectory;
    if(!Core::Assets::ResolvePathFromCookRoot(outPaths.repoRoot, requestedCacheDirectory, "cache directory", outPaths.cacheDirectory, outError))
        return false;

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


static bool BuildIncludeDirectories(const Path& repoRoot, const Core::ShaderCook::ManifestData& manifest, Core::ShaderCook::CookVector<Path>& outIncludeDirectories, AString& outError){
    ErrorCode errorCode;

    outIncludeDirectories.clear();
    outIncludeDirectories.reserve(manifest.includeRoots.size());

    for(const AString& includeRoot : manifest.includeRoots){
        Path includeDirectory;
        if(!ResolveAbsolutePath(repoRoot, includeRoot, includeDirectory, errorCode)){
            outError = StringFormat("Failed to resolve include_root '{}'", includeRoot);
            return false;
        }

        outIncludeDirectories.push_back(Move(includeDirectory));
    }

    return true;
}


static VariantCachePaths BuildVariantCachePaths(const Path& cacheDirectory, const AStringView configurationSafeName, const Core::ShaderCook::ManifestEntry& entry, const AStringView variantName){
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


static bool GetVariantBytecode(
    const Core::ShaderCook::ManifestEntry& entry,
    const AStringView variantName,
    const Core::ShaderCook::DefineCombo& defineCombo,
    const Core::ShaderCook::CookVector<Path>& includeDirectories,
    const Path& sourcePath,
    const VariantCachePaths& cachePaths,
    const AStringView sourceChecksumHex,
    Core::Alloc::CustomArena& cookArena,
    Core::IShaderCompiler& shaderCompiler,
    Core::ShaderCook::CookVector<u8>& outBytecode,
    AString& outError
)
{
    ErrorCode errorCode;

    outBytecode.clear();

    const bool cacheUpToDate =
        FileExists(cachePaths.bytecodePath, errorCode)
        && FileExists(cachePaths.sourceChecksumPath, errorCode)
        && Core::Assets::CookSourceChecksumMatches(cachePaths.sourceChecksumPath, sourceChecksumHex);
    if(cacheUpToDate){
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
    if(!shaderCompiler.compileVariant(compileRequest, outBytecode, outError))
        return false;

    CreateDirectories(cachePaths.bytecodePath.parent_path(), errorCode);
    if(errorCode){
        outError = StringFormat(
            "Failed to create cache directory '{}': {}",
            PathToString(cachePaths.bytecodePath.parent_path()),
            errorCode.message()
        );
        return false;
    }

    if(!WriteBinaryFile(cachePaths.bytecodePath, outBytecode)){
        outError = StringFormat(
            "Failed to write shader bytecode cache '{}' for entry '{}'",
            PathToString(cachePaths.bytecodePath),
            entry.name
        );
        return false;
    }

    if(!Core::Assets::WriteCookSourceChecksum(cachePaths.sourceChecksumPath, sourceChecksumHex, outError))
        return false;

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
)
{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShaderAssetCooker::CookShaderAssets(const ShaderCookEnvironment& environment, Core::Alloc::CustomArena& cookArena, ShaderCookResult& outResult, AString& outError){
    outResult = {};
    outError.clear();

    __hidden_assets::ResolvedCookPaths resolvedPaths;
    if(!__hidden_assets::ResolveCookPaths(environment, resolvedPaths, outError))
        return false;

    Core::ShaderCook::ManifestData manifest(cookArena);
    if(!Core::ShaderCook::ParseManifestFile(resolvedPaths.manifestPath, cookArena, manifest, outError))
        return false;

    Core::ShaderCook::CookVector<Path> includeDirectories(Core::ShaderCook::CookAllocator<Path>(cookArena));
    if(!__hidden_assets::BuildIncludeDirectories(resolvedPaths.repoRoot, manifest, includeDirectories, outError))
        return false;

    const AString normalizedConfiguration = Core::Assets::NormalizeCookConfiguration(environment.configuration);
    const AString configurationSafeName = BuildSafeCacheName(normalizedConfiguration);
    Vector<Core::ShaderArchive::Record> shaderIndexRecords;
    Core::ShaderCook::CookHashSet<NameHash> seenVirtualPathHashes(Core::ShaderCook::CookAllocator<NameHash>(cookArena));

    Core::Filesystem::VolumeBuildConfig volumeConfig;
    volumeConfig.volumeName = manifest.volumeName;
    volumeConfig.segmentSize = manifest.segmentSize;
    volumeConfig.metadataSize = manifest.metadataSize;

    Core::Filesystem::VolumeSession volumeSession(cookArena);
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

    ErrorCode errorCode;
    for(const Core::ShaderCook::ManifestEntry& entry : manifest.entries){
        Path sourcePath;
        if(!ResolveAbsolutePath(resolvedPaths.repoRoot, entry.source, sourcePath, errorCode)){
            outError = StringFormat(
                "Failed to resolve source path '{}' for entry '{}'",
                entry.source,
                entry.name
            );
            return false;
        }

        if(!FileExists(sourcePath, errorCode)){
            outError = StringFormat(
                "Shader source does not exist for entry '{}': '{}'",
                entry.name,
                PathToString(sourcePath)
            );
            return false;
        }

        Core::ShaderCook::CookVector<Path> dependencies(Core::ShaderCook::CookAllocator<Path>(cookArena));
        if(!Core::ShaderCook::GatherShaderDependencies(sourcePath, includeDirectories, cookArena, dependencies, outError))
            return false;

        Core::ShaderCook::CookVector<Core::ShaderCook::DefineCombo> defineCombinations(Core::ShaderCook::CookAllocator<Core::ShaderCook::DefineCombo>(cookArena));
        Core::ShaderCook::ExpandDefineCombinations(entry.defineValues, cookArena, defineCombinations);
        if(defineCombinations.empty())
            defineCombinations.push_back(Core::ShaderCook::DefineCombo(Core::ShaderCook::CookAllocator<Pair<const AString, AString>>(cookArena)));

        for(const Core::ShaderCook::DefineCombo& defineCombo : defineCombinations){
            const AString generatedVariantName = Core::ShaderCook::BuildVariantName(defineCombo, cookArena);
            const AString variantName = __hidden_assets::NormalizeVariantName(entry, generatedVariantName);

            u64 sourceChecksum = 0;
            if(!Core::ShaderCook::ComputeSourceChecksum(entry, variantName, dependencies, cookArena, sourceChecksum, outError))
                return false;
            const AString sourceChecksumHex = FormatHex64(sourceChecksum);

            const __hidden_assets::VariantCachePaths cachePaths = __hidden_assets::BuildVariantCachePaths(
                resolvedPaths.cacheDirectory,
                configurationSafeName,
                entry,
                variantName
            );
            Core::ShaderCook::CookVector<u8> cookedBytecode(Core::ShaderCook::CookAllocator<u8>(cookArena));
            if(!__hidden_assets::GetVariantBytecode(
                entry,
                variantName,
                defineCombo,
                includeDirectories,
                sourcePath,
                cachePaths,
                sourceChecksumHex,
                cookArena,
                *shaderCompiler,
                cookedBytecode,
                outError
            )){
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

    if(!options.cookArena){
        outError = "ShaderAssetCooker::cook failed: cookArena is null";
        return false;
    }

    ShaderCookEnvironment environment;
    environment.configuration = options.configuration;
    environment.repoRoot = options.repoRoot.empty() ? Path(".") : Path(options.repoRoot);
    environment.manifestPath = Path(options.manifest);
    environment.outputDirectory = Path(options.outputDirectory);
    environment.cacheDirectory = options.cacheDirectory.empty() ? Path() : Path(options.cacheDirectory);

    ShaderCookResult result;
    if(!CookShaderAssets(environment, *options.cookArena, result, outError))
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

