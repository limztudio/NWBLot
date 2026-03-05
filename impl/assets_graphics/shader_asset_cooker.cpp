// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_asset_cooker.h"

#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_cook.h>

#include <core/filesystem/filesystem.h>
#include <core/alloc/core.h>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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
static bool ResolveCookPaths(const ShaderCookEnvironment& environment, ResolvedCookPaths& outPaths){
    ErrorCode errorCode;

    outPaths = {};

    if(environment.manifestPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker::CookShaderAssets failed: manifest path is empty"));
        return false;
    }
    if(environment.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker::CookShaderAssets failed: output directory is empty"));
        return false;
    }

    outPaths.repoRoot = environment.repoRoot.empty() ? Path(".") : environment.repoRoot;
    outPaths.repoRoot = AbsolutePath(outPaths.repoRoot, errorCode).lexically_normal();
    if(errorCode){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker::CookShaderAssets failed to resolve repo root: {}"),
            StringConvert(errorCode.message())
        );
        return false;
    }

    if(!ResolveAbsolutePath(outPaths.repoRoot, PathToString(environment.manifestPath), outPaths.manifestPath, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker::CookShaderAssets failed to resolve manifest path '{}'"),
            PathToString<tchar>(environment.manifestPath)
        );
        return false;
    }
    if(!ResolveAbsolutePath(outPaths.repoRoot, PathToString(environment.outputDirectory), outPaths.outputDirectory, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker::CookShaderAssets failed to resolve output directory '{}'"),
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
            NWB_TEXT("ShaderAssetCooker::CookShaderAssets failed to resolve cache directory '{}'"),
            PathToString<tchar>(requestedCacheDirectory)
        );
        return false;
    }

    if(!CreateDirectories(outPaths.cacheDirectory, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker::CookShaderAssets failed to create cache directory '{}': {}"),
            PathToString<tchar>(outPaths.cacheDirectory),
            StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}


static bool BuildIncludeDirectories(const Path& repoRoot, const Core::ShaderCook::ManifestData& manifest, Core::ShaderCook::CookVector<Path>& outIncludeDirectories){
    ErrorCode errorCode;

    outIncludeDirectories.clear();
    outIncludeDirectories.reserve(manifest.includeRoots.size());

    for(const AString& includeRoot : manifest.includeRoots){
        Path includeDirectory;
        if(!ResolveAbsolutePath(repoRoot, includeRoot, includeDirectory, errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker::CookShaderAssets failed: failed to resolve include_root '{}': {}"), StringConvert(includeRoot), StringConvert(errorCode.message()));
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
    Core::ShaderCook& shaderCook,
    Vector<u8>& outBytecode
)
{
    ErrorCode errorCode;

    outBytecode.clear();

    const bool cacheUpToDate = [&]() -> bool {
        if(!FileExists(cachePaths.bytecodePath, errorCode) || !FileExists(cachePaths.sourceChecksumPath, errorCode))
            return false;
        AString cachedText;
        return ReadTextFile(cachePaths.sourceChecksumPath, cachedText) && (Trim(cachedText) == sourceChecksumHex);
    }();
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
    if(!shaderCook.compileVariant(compileRequest, outBytecode))
        return false;

    if(!CreateDirectories(cachePaths.bytecodePath.parent_path(), errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker::CookShaderAssets failed to create cache directory '{}': {}"),
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


static bool EmitShaderArchiveRecord(
    const Core::ShaderCook::ManifestEntry& entry,
    const AStringView variantName,
    const u64 sourceChecksum,
    const Vector<u8>& bytecode,
    Core::ShaderCook::CookHashSet<NameHash>& inOutVirtualPathHashes,
    Core::Filesystem::VolumeSession& volumeSession,
    Vector<Core::ShaderArchive::Record>& inOutRecords
)
{
    const AString variantNameText(variantName);
    const AString virtualPath = Core::ShaderArchive::buildVirtualPath(entry.name, variantName);
    const NameHash virtualPathHash = Name(virtualPath.c_str()).hash();
    if(!inOutVirtualPathHashes.insert(virtualPathHash).second){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Shader cook produced duplicate virtual path '{}' (entry='{}', variant='{}')"),
            StringConvert(virtualPath),
            StringConvert(entry.name),
            StringConvert(variantName)
        );
        return false;
    }

    {
        AString localError;
        if(!volumeSession.pushData(virtualPath, bytecode, localError)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to push shader bytecode '{}': {}"),
                StringConvert(virtualPath),
                StringConvert(localError)
            );
            return false;
        }
    }

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


bool ShaderAssetCooker::cook(const Core::Assets::AssetCookOptions& options){
    ShaderCookEnvironment environment;
    environment.configuration = options.configuration;
    environment.repoRoot = options.repoRoot.empty() ? Path(".") : Path(options.repoRoot);
    environment.manifestPath = Path(options.manifest);
    environment.outputDirectory = Path(options.outputDirectory);
    environment.cacheDirectory = options.cacheDirectory.empty() ? Path() : Path(options.cacheDirectory);

    ShaderCookResult result;
    if(!cookShaderAssets(environment, result))
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


bool ShaderAssetCooker::cookShaderAssets(const ShaderCookEnvironment& environment, ShaderCookResult& outResult){
    outResult = {};

    Core::ShaderCook shaderCook(m_arena);

    __hidden_assets::ResolvedCookPaths resolvedPaths;
    if(!__hidden_assets::ResolveCookPaths(environment, resolvedPaths))
        return false;

    Core::ShaderCook::ManifestData manifest(m_arena);
    if(!shaderCook.parseManifestFile(resolvedPaths.manifestPath, manifest))
        return false;

    Core::ShaderCook::CookVector<Path> includeDirectories{Core::ShaderCook::CookAllocator<Path>(m_arena)};
    if(!__hidden_assets::BuildIncludeDirectories(resolvedPaths.repoRoot, manifest, includeDirectories))
        return false;

    AString normalizedConfiguration = CanonicalizeText(environment.configuration);
    if(normalizedConfiguration.empty())
        normalizedConfiguration = "default";
    const AString configurationSafeName = BuildSafeCacheName(normalizedConfiguration);
    Vector<Core::ShaderArchive::Record> shaderIndexRecords;
    Core::ShaderCook::CookHashSet<NameHash> seenVirtualPathHashes{Core::ShaderCook::CookAllocator<NameHash>(m_arena)};

    Core::Filesystem::VolumeBuildConfig volumeConfig;
    volumeConfig.volumeName = manifest.volumeName;
    volumeConfig.segmentSize = manifest.segmentSize;
    volumeConfig.metadataSize = manifest.metadataSize;

    Core::Filesystem::VolumeSession volumeSession(m_arena);
    {
        AString localError;
        if(!volumeSession.create(resolvedPaths.outputDirectory, volumeConfig, localError)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker::CookShaderAssets failed to create volume session: {}"), StringConvert(localError));
            return false;
        }
    }

    ErrorCode errorCode;
    for(const Core::ShaderCook::ManifestEntry& entry : manifest.entries){
        Path sourcePath;
        if(!ResolveAbsolutePath(resolvedPaths.repoRoot, entry.source, sourcePath, errorCode)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to resolve source path '{}' for entry '{}'"),
                StringConvert(entry.source),
                StringConvert(entry.name)
            );
            return false;
        }

        if(!FileExists(sourcePath, errorCode)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Shader source does not exist for entry '{}': '{}'"),
                StringConvert(entry.name),
                PathToString<tchar>(sourcePath)
            );
            return false;
        }

        Core::ShaderCook::CookVector<Path> dependencies{Core::ShaderCook::CookAllocator<Path>(m_arena)};
        if(!shaderCook.gatherShaderDependencies(sourcePath, includeDirectories, dependencies))
            return false;

        Core::ShaderCook::CookVector<Core::ShaderCook::DefineCombo> defineCombinations{Core::ShaderCook::CookAllocator<Core::ShaderCook::DefineCombo>(m_arena)};
        shaderCook.expandDefineCombinations(entry.defineValues, defineCombinations);
        if(defineCombinations.empty())
            defineCombinations.push_back(Core::ShaderCook::DefineCombo(Core::ShaderCook::CookAllocator<Pair<const AString, AString>>(m_arena)));

        for(const Core::ShaderCook::DefineCombo& defineCombo : defineCombinations){
            const AString generatedVariantName = shaderCook.buildVariantName(defineCombo);
            const AString variantName = __hidden_assets::NormalizeVariantName(entry, generatedVariantName);

            u64 sourceChecksum = 0;
            if(!shaderCook.computeSourceChecksum(entry, variantName, dependencies, sourceChecksum))
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
                includeDirectories,
                sourcePath,
                cachePaths,
                sourceChecksumHex,
                shaderCook,
                cookedBytecode
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
                shaderIndexRecords
            )){
                return false;
            }
        }
    }

    Vector<u8> indexBinary;
    if(!Core::ShaderArchive::serializeIndex(shaderIndexRecords, indexBinary)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker::CookShaderAssets failed to serialize shader index"));
        return false;
    }
    {
        AString localError;
        if(!volumeSession.pushData(Core::ShaderArchive::s_IndexVirtualPath, indexBinary, localError)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker::CookShaderAssets failed to push shader index: {}"), StringConvert(localError));
            return false;
        }
    }

    outResult.volumeName = manifest.volumeName;
    outResult.fileCount = volumeSession.fileCount();
    outResult.segmentCount = static_cast<u64>(volumeSession.segmentCount());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
