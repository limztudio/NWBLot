// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_paths.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ResolveCookPaths(
    const AssetBuildOptions& options,
    Assets::ResolvedCookPaths& outPaths,
    Assets::ScratchArena& scratchArena){
    ErrorCode errorCode;

    outPaths.repoRoot.clear();
    outPaths.assetRoots.clear();
    outPaths.outputDirectory.clear();
    outPaths.cacheDirectory.clear();

    if(options.assetRoots.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: no asset roots specified"));
        return false;
    }
    if(options.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: output directory is empty"));
        return false;
    }

    outPaths.repoRoot = options.repoRoot.empty() ? Path(outPaths.repoRoot.arena(), ".") : Path(outPaths.repoRoot.arena(), options.repoRoot.c_str());
    outPaths.repoRoot = AbsolutePath(outPaths.repoRoot, errorCode).lexically_normal();
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve repo root: {}"), StringConvert(errorCode.message()));
        return false;
    }

    outPaths.assetRoots.reserve(options.assetRoots.size());
    for(const AssetBuildRoot& assetRoot : options.assetRoots){
        if(assetRoot.virtualRoot.view() != Assets::s_EngineVirtualRoot && assetRoot.virtualRoot.view() != Assets::s_ProjectVirtualRoot){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: asset root '{}' uses unsupported virtual root '{}'")
                , StringConvert(assetRoot.path)
                , StringConvert(assetRoot.virtualRoot.c_str())
            );
            outPaths.assetRoots.clear();
            return false;
        }

        Path resolvedAssetRoot(outPaths.repoRoot.arena());
        const Assets::ScratchString assetRootText(assetRoot.path, scratchArena);
        errorCode.clear();
        if(!ResolveAbsolutePath(outPaths.repoRoot, assetRootText, resolvedAssetRoot, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve asset root '{}': {}")
                    , StringConvert(assetRoot.path)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: asset root is empty or invalid: '{}'")
                    , StringConvert(assetRoot.path)
                );
            }
            outPaths.assetRoots.clear();
            return false;
        }

        outPaths.assetRoots.emplace_back(Move(resolvedAssetRoot), assetRoot.virtualRoot);
    }

    errorCode.clear();
    {
        const Assets::ScratchString outputDirectoryText(options.outputDirectory, scratchArena);
        if(!ResolveAbsolutePath(outPaths.repoRoot, outputDirectoryText, outPaths.outputDirectory, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve output directory '{}': {}")
                    , StringConvert(options.outputDirectory)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: output directory is empty or invalid: '{}'")
                    , StringConvert(options.outputDirectory)
                );
            }
            return false;
        }
    }

    const Path defaultCacheDirectory = outPaths.repoRoot / "__build_obj/asset_cache";
    const Assets::AssetString& requestedCacheDirectory = options.cacheDirectory;
    errorCode.clear();
    {
        const Assets::ScratchString requestedCacheDirectoryText = requestedCacheDirectory.empty()
            ? PathToString(scratchArena, defaultCacheDirectory)
            : Assets::ScratchString(requestedCacheDirectory, scratchArena)
        ;
        if(!ResolveAbsolutePath(outPaths.repoRoot, requestedCacheDirectoryText, outPaths.cacheDirectory, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve cache directory '{}': {}")
                    , StringConvert(requestedCacheDirectoryText)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: cache directory is empty or invalid: '{}'")
                    , StringConvert(requestedCacheDirectoryText)
                );
            }
            return false;
        }
    }

    if(!EnsureDirectories(outPaths.cacheDirectory, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to create cache directory '{}': {}")
            , PathToString<tchar>(outPaths.cacheDirectory)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

