// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "build.h"
#include "../root_discovery.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Assets::ResolvedCookPaths> ResolveCookPaths(const AssetBuildOptions& options, Assets::AssetArena& arena){
    if(options.assetRoots.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: no asset roots specified"));
        return MakeUnexpected(Failure{});
    }
    if(options.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: output directory is empty"));
        return MakeUnexpected(Failure{});
    }

    Assets::ResolvedCookPaths paths(arena);
    if(!options.resolvedRepoRoot.empty())
        paths.repoRoot = options.resolvedRepoRoot;
    else{
        auto repoRoot = ResolvePipelineRepoRoot(AStringView(options.repoRoot), NWB_TEXT("AssetBuilder"), arena);
        if(!repoRoot)
            return MakeUnexpected(Failure{});
        paths.repoRoot = Move(*repoRoot);
    }

    paths.assetRoots.reserve(options.assetRoots.size());
    for(const AssetBuildRoot& assetRoot : options.assetRoots){
        if(assetRoot.virtualRoot.view() != Assets::s_EngineVirtualRoot && assetRoot.virtualRoot.view() != Assets::s_ProjectVirtualRoot){
            if(!assetRoot.resolvedPath.empty())
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: asset root '{}' uses unsupported virtual root '{}'"), PathToString<tchar>(assetRoot.resolvedPath), ConvertString(assetRoot.virtualRoot.view()));
            else
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: asset root '{}' uses unsupported virtual root '{}'"), ConvertString(assetRoot.path), ConvertString(assetRoot.virtualRoot.view()));
            return MakeUnexpected(Failure{});
        }
        if(!assetRoot.resolvedPath.empty()){
            paths.assetRoots.emplace_back(Path(arena, assetRoot.resolvedPath), assetRoot.virtualRoot);
            continue;
        }
        auto resolvedAssetRoot = ResolveAbsolutePath(arena, paths.repoRoot, AStringView(assetRoot.path));
        if(!resolvedAssetRoot){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve asset root '{}': {}"), ConvertString(assetRoot.path), ConvertString(resolvedAssetRoot.error().message()));
            return MakeUnexpected(Failure{});
        }
        paths.assetRoots.emplace_back(Move(*resolvedAssetRoot), assetRoot.virtualRoot);
    }

    auto outputDirectory = ResolveAbsolutePath(arena, paths.repoRoot, AStringView(options.outputDirectory));
    if(!outputDirectory){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve output directory '{}': {}"), ConvertString(options.outputDirectory), ConvertString(outputDirectory.error().message()));
        return MakeUnexpected(Failure{});
    }
    paths.outputDirectory = Move(*outputDirectory);

    paths.cacheDirectory = paths.repoRoot / s_AssetCacheDirectory;
    const auto created = EnsureDirectories(paths.cacheDirectory);
    if(!created){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to create cache directory '{}': {}"), PathToString<tchar>(paths.cacheDirectory), ConvertString(created.error().message()));
        return MakeUnexpected(Failure{});
    }
    return paths;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

