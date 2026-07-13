
#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_paths.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ResolveCookPaths(
    const AssetCookOptions& options,
    ResolvedCookPaths& outPaths,
    ScratchArena& scratchArena
){
    ErrorCode errorCode;

    outPaths.repoRoot.clear();
    outPaths.assetRoots.clear();
    outPaths.outputDirectory.clear();
    outPaths.cacheDirectory.clear();

    if(options.assetRoots.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: no asset roots specified"));
        return false;
    }
    if(options.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: output directory is empty"));
        return false;
    }

    outPaths.repoRoot = options.repoRoot.empty() ? Path(outPaths.repoRoot.arena(), ".") : Path(outPaths.repoRoot.arena(), options.repoRoot.c_str());
    outPaths.repoRoot = AbsolutePath(outPaths.repoRoot, errorCode).lexically_normal();
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to resolve repo root: {}"), StringConvert(errorCode.message()));
        return false;
    }

    outPaths.assetRoots.reserve(options.assetRoots.size());
    for(const AssetCookRoot& assetRoot : options.assetRoots){
        if(assetRoot.virtualRoot.view() != s_EngineVirtualRoot && assetRoot.virtualRoot.view() != s_ProjectVirtualRoot){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: asset root '{}' uses unsupported virtual root '{}'")
                , StringConvert(assetRoot.path)
                , StringConvert(assetRoot.virtualRoot.c_str())
            );
            outPaths.assetRoots.clear();
            return false;
        }

        Path resolvedAssetRoot(outPaths.repoRoot.arena());
        const ScratchString assetRootText(assetRoot.path, scratchArena);
        errorCode.clear();
        if(!ResolveAbsolutePath(outPaths.repoRoot, assetRootText, resolvedAssetRoot, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to resolve asset root '{}': {}")
                    , StringConvert(assetRoot.path)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: asset root is empty or invalid: '{}'")
                    , StringConvert(assetRoot.path)
                );
            }
            outPaths.assetRoots.clear();
            return false;
        }

        auto assetRootName = PathToString(scratchArena, resolvedAssetRoot.filename());
        CanonicalizeTextInPlace(assetRootName);
        if(assetRootName != s_AssetsDirectoryName){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: asset root must point to an 'assets' directory: '{}'")
                , PathToString<tchar>(resolvedAssetRoot)
            );
            outPaths.assetRoots.clear();
            return false;
        }

        outPaths.assetRoots.emplace_back(Move(resolvedAssetRoot), assetRoot.virtualRoot);
    }

    errorCode.clear();
    {
        const ScratchString outputDirectoryText(options.outputDirectory, scratchArena);
        if(!ResolveAbsolutePath(outPaths.repoRoot, outputDirectoryText, outPaths.outputDirectory, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to resolve output directory '{}': {}")
                    , StringConvert(options.outputDirectory)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: output directory is empty or invalid: '{}'")
                    , StringConvert(options.outputDirectory)
                );
            }
            return false;
        }
    }

    const Path defaultCacheDirectory = outPaths.repoRoot / "__build_obj/asset_cache";
    const AssetString& requestedCacheDirectory = options.cacheDirectory;
    errorCode.clear();
    {
        const ScratchString requestedCacheDirectoryText = requestedCacheDirectory.empty()
            ? PathToString(scratchArena, defaultCacheDirectory)
            : ScratchString(requestedCacheDirectory, scratchArena)
        ;
        if(!ResolveAbsolutePath(outPaths.repoRoot, requestedCacheDirectoryText, outPaths.cacheDirectory, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to resolve cache directory '{}': {}")
                    , StringConvert(requestedCacheDirectoryText)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: cache directory is empty or invalid: '{}'")
                    , StringConvert(requestedCacheDirectoryText)
                );
            }
            return false;
        }
    }

    if(!EnsureDirectories(outPaths.cacheDirectory, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to create cache directory '{}': {}")
            , PathToString<tchar>(outPaths.cacheDirectory)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

