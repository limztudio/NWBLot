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


namespace AssetsVolumeCookDetail{


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
    const AssetVolumeCookEnvironment& environment,
    ResolvedCookPaths& outPaths,
    ScratchArena& scratchArena
){
    ErrorCode errorCode;

    outPaths.repoRoot.clear();
    outPaths.assetRoots.clear();
    outPaths.outputDirectory.clear();
    outPaths.cacheDirectory.clear();

    if(environment.assetRoots.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: no asset roots specified"));
        return false;
    }
    if(environment.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: output directory is empty"));
        return false;
    }

    outPaths.repoRoot = environment.repoRoot.empty() ? Path(".") : environment.repoRoot;
    outPaths.repoRoot = AbsolutePath(outPaths.repoRoot, errorCode).lexically_normal();
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to resolve repo root: {}"), StringConvert(errorCode.message()));
        return false;
    }

    outPaths.assetRoots.reserve(environment.assetRoots.size());
    for(const Path& assetRoot : environment.assetRoots){
        Path resolvedAssetRoot;
        const ScratchString assetRootText = PathToString(scratchArena, assetRoot);
        errorCode.clear();
        if(!ResolveAbsolutePath(outPaths.repoRoot, assetRootText, resolvedAssetRoot, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to resolve asset root '{}': {}")
                    , PathToString<tchar>(assetRoot)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: asset root is empty or invalid: '{}'")
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
                NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to resolve output directory '{}': {}")
                    , PathToString<tchar>(environment.outputDirectory)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: output directory is empty or invalid: '{}'")
                    , PathToString<tchar>(environment.outputDirectory)
                );
            }
            return false;
        }
    }

    const Path defaultCacheDirectory = outPaths.repoRoot / "__build_obj/asset_cache";
    const Path requestedCacheDirectory = environment.cacheDirectory.empty()
        ? defaultCacheDirectory
        : environment.cacheDirectory
    ;
    errorCode.clear();
    {
        const ScratchString requestedCacheDirectoryText = PathToString(scratchArena, requestedCacheDirectory);
        if(!ResolveAbsolutePath(outPaths.repoRoot, requestedCacheDirectoryText, outPaths.cacheDirectory, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to resolve cache directory '{}': {}")
                    , PathToString<tchar>(requestedCacheDirectory)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: cache directory is empty or invalid: '{}'")
                    , PathToString<tchar>(requestedCacheDirectory)
                );
            }
            return false;
        }
    }

    if(!EnsureDirectories(outPaths.cacheDirectory, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to create cache directory '{}': {}")
            , PathToString<tchar>(outPaths.cacheDirectory)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool DiscoverFilesWithExtension(
    const CookVector<Path>& assetRoots,
    const AStringView expectedExtension,
    DiscoveredNwbFileVector& outFiles,
    ScratchArena& scratchArena
){
    CookArena& cookArena = outFiles.get_allocator().arena();
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
                NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to query asset root '{}': {}")
                    , PathToString<tchar>(assetRoot)
                    , StringConvert(errorCode.message())
                );
                return false;
            }

            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: asset root is not a directory: '{}'")
                , PathToString<tchar>(assetRoot)
            );
            return false;
        }

        for(const auto& dirEntry : RecursiveDirectoryIterator(assetRoot, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: error scanning asset root '{}': {}")
                    , PathToString<tchar>(assetRoot)
                    , StringConvert(errorCode.message())
                );
                return false;
            }

            errorCode.clear();
            const bool isRegularFile = dirEntry.is_regular_file(errorCode);
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to inspect '{}' while scanning '{}': {}")
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

