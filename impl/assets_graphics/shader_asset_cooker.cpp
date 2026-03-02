// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_asset_cooker.h"

#if defined(NWB_COOK)
#include "shader_asset.h"

#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_compile.h>
#include <core/graphics/shader_cook.h>

#include <core/filesystem/filesystem.h>
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShaderAssetCooker::CookShaderAssets(const ShaderCookEnvironment& environment, ShaderCookResult& outResult, AString& outError){
    outResult = {};
    outError.clear();

    if(environment.manifestPath.empty()){
        outError = "ShaderAssetCooker::CookShaderAssets failed: manifest path is empty";
        return false;
    }
    if(environment.outputDirectory.empty()){
        outError = "ShaderAssetCooker::CookShaderAssets failed: output directory is empty";
        return false;
    }

    ErrorCode errorCode;

    Path repoRoot = environment.repoRoot;
    if(repoRoot.empty())
        repoRoot = ".";
    repoRoot = AbsolutePath(repoRoot, errorCode).lexically_normal();
    if(errorCode){
        outError = StringFormat("ShaderAssetCooker::CookShaderAssets failed to resolve repo root: {}", errorCode.message());
        return false;
    }

    Path manifestPath;
    if(!ResolveAbsolutePath(
        repoRoot,
        PathToString(environment.manifestPath),
        manifestPath
    )){
        outError = StringFormat(
            "ShaderAssetCooker::CookShaderAssets failed to resolve manifest path '{}'",
            PathToString(environment.manifestPath)
        );
        return false;
    }

    Path outputDirectory;
    if(!ResolveAbsolutePath(
        repoRoot,
        PathToString(environment.outputDirectory),
        outputDirectory
    )){
        outError = StringFormat(
            "ShaderAssetCooker::CookShaderAssets failed to resolve output directory '{}'",
            PathToString(environment.outputDirectory)
        );
        return false;
    }

    Path cacheDirectory = environment.cacheDirectory;
    if(cacheDirectory.empty())
        cacheDirectory = repoRoot / "__build_obj/shader_cache";
    if(!ResolveAbsolutePath(
        repoRoot,
        PathToString(cacheDirectory),
        cacheDirectory
    )){
        outError = StringFormat(
            "ShaderAssetCooker::CookShaderAssets failed to resolve cache directory '{}'",
            PathToString(environment.cacheDirectory)
        );
        return false;
    }

    CreateDirectories(cacheDirectory, errorCode);
    if(errorCode){
        outError = StringFormat(
            "ShaderAssetCooker::CookShaderAssets failed to create cache directory '{}': {}",
            PathToString(cacheDirectory),
            errorCode.message()
        );
        return false;
    }

    Core::ShaderCook::ManifestData manifest;
    if(!Core::ShaderCook::ParseManifestFile(manifestPath, manifest, outError))
        return false;

    Core::ShaderCook::CookVector<Path> includeDirectories;
    includeDirectories.reserve(manifest.includeRoots.size());
    for(const AString& includeRoot : manifest.includeRoots){
        Path includeDirectory;
        if(!ResolveAbsolutePath(repoRoot, includeRoot, includeDirectory)){
            outError = StringFormat("Failed to resolve include_root '{}'", includeRoot);
            return false;
        }
        includeDirectories.push_back(includeDirectory);
    }

    const AString normalizedConfiguration = __hidden_assets::NormalizeCookConfiguration(environment.configuration);
    const AString configurationSafeName = BuildSafeCacheName(normalizedConfiguration);
    Core::ShaderCook::CookMap<AString, Core::ShaderCook::CookVector<u8>> volumeFiles;
    AString shaderIndexText = "NWB_SHADER_INDEX_V1\n";

    Core::ShaderCompile shaderCompile;
    Core::IShaderCompiler* const shaderCompiler = shaderCompile.getCompiler();

    for(const Core::ShaderCook::ManifestEntry& entry : manifest.entries){
        Path sourcePath;
        if(!ResolveAbsolutePath(repoRoot, entry.source, sourcePath)){
            outError = StringFormat(
                "Failed to resolve source path '{}' for entry '{}'",
                entry.source,
                entry.name
            );
            return false;
        }

        if(!FileExists(sourcePath, errorCode) || errorCode){
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

            const AString shaderSafeName = BuildSafeCacheName(entry.name);
            const AString variantHashHex = FormatHex64(ComputeFnv64Text(variantName));
            const Path cachePath = cacheDirectory / configurationSafeName / StringFormat(
                "{}__{}__{}.spv",
                shaderSafeName,
                entry.stage,
                variantHashHex
            );
            Path cacheSourceChecksumPath = cachePath;
            cacheSourceChecksumPath += ".source";

            const bool cacheBytecodeExists = FileExists(cachePath, errorCode);
            const bool cacheChecksumExists = FileExists(cacheSourceChecksumPath, errorCode);
            const bool cacheUpToDate = cacheBytecodeExists
                && cacheChecksumExists
                && __hidden_assets::CacheSourceChecksumMatches(cacheSourceChecksumPath, sourceChecksumHex);

            if(!cacheUpToDate){
                const Core::ShaderCompilerRequest compileRequest = {
                    entry.name,
                    entry.compiler,
                    entry.stage,
                    entry.targetProfile,
                    entry.entryPoint,
                    defineCombo,
                    includeDirectories,
                    sourcePath,
                    cachePath
                };
                if(!shaderCompiler->compileVariant(compileRequest, outError))
                    return false;
            }

            Core::ShaderCook::CookVector<u8> cookedBytecode;
            if(!ReadBinaryFile(cachePath, cookedBytecode)){
                outError = StringFormat(
                    "Failed to read shader bytecode cache '{}' for entry '{}'",
                    PathToString(cachePath),
                    entry.name
                );
                return false;
            }

            const u64 bytecodeChecksum = ComputeFnv64Bytes(
                cookedBytecode.empty() ? nullptr : cookedBytecode.data(),
                cookedBytecode.size()
            );
            const AString bytecodeChecksumHex = FormatHex64(bytecodeChecksum);

            if(!__hidden_assets::WriteCacheSourceChecksum(cacheSourceChecksumPath, sourceChecksumHex, outError))
                return false;

            Shader shaderAsset;
            const AString virtualPath = Core::ShaderArchive::buildVirtualPath(entry.name, variantName);
            shaderAsset.setMetadata(
                Name(entry.name.c_str()),
                Name(variantName.c_str()),
                entry.stage,
                entry.entryPoint,
                sourceChecksumHex,
                bytecodeChecksumHex,
                virtualPath
            );

            AString indexLine;
            if(!shaderAsset.buildIndexLine(indexLine, outError))
                return false;
            shaderIndexText += indexLine;
            shaderIndexText += '\n';

            volumeFiles[virtualPath] = Move(cookedBytecode);
        }
    }

    volumeFiles[Core::ShaderArchive::s_IndexVirtualPath] = Core::ShaderCook::CookVector<u8>(
        shaderIndexText.begin(),
        shaderIndexText.end()
    );

    Core::Filesystem::VolumeBuildConfig volumeConfig;
    volumeConfig.volumeName = manifest.volumeName;
    volumeConfig.segmentSize = manifest.segmentSize;
    volumeConfig.metadataSize = manifest.metadataSize;

    Core::Filesystem::VolumeBuildInfo buildInfo;
    if(!Core::Filesystem::BuildVolume(outputDirectory, volumeConfig, volumeFiles, buildInfo, outError))
        return false;

    outResult.volumeName = manifest.volumeName;
    outResult.fileCount = buildInfo.fileCount;
    outResult.segmentCount = buildInfo.segmentCount;
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
