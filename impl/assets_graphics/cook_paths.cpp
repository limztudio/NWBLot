// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_paths.h"

#include <core/assets/paths.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_paths{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsGraphicsCookDetail;

using IncludeDirectoryScratchSet = HashSet<ScratchString, Hasher<ScratchString>, EqualTo<ScratchString>, ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StagedVolumePaths BuildStagedVolumePaths(
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ResolveCookPaths(
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool DiscoverFilesWithExtension(
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildIncludeDirectories(
    const Path& repoRoot,
    const ShaderCook::CookVector<Path>& assetRoots,
    const Path& materialBindIncludeRoot,
    const ShaderCook::ShaderEntry& entry,
    ShaderCook::CookVector<Path>& outIncludeDirectories,
    ScratchArena& scratchArena
){
    ErrorCode errorCode;
    __hidden_cook_paths::IncludeDirectoryScratchSet seenIncludeDirectories(
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
        if(!__hidden_cook_paths::AppendIncludeDirectory(materialBindIncludeRoot, entry, seenIncludeDirectories, outIncludeDirectories, scratchArena))
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

        if(!__hidden_cook_paths::AppendIncludeDirectory(includeDirectory, entry, seenIncludeDirectories, outIncludeDirectories, scratchArena))
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

