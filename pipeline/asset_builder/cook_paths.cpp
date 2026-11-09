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
    if(!ValidateAssetBuilderPathInputs(!options.assetRoots.empty(), !options.outputDirectory.empty()))
        return MakeUnexpected(Failure{});

    Assets::ResolvedCookPaths paths(arena);
    auto repoRoot = ResolvePipelineRepoRoot(options.repoRoot, FOUNDATION_TEXT("AssetBuilder"), arena);
    if(!repoRoot)
        return MakeUnexpected(Failure{});
    paths.repoRoot = Move(*repoRoot);

    paths.assetRoots.reserve(options.assetRoots.size());
    for(const AssetBuildRoot& assetRoot : options.assetRoots){
        if(!IsValidAssetBuildVirtualRoot(assetRoot.virtualRoot.view(), Assets::s_EngineVirtualRoot, Assets::s_ProjectVirtualRoot)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetBuilder: asset root '{}' uses unsupported virtual root '{}'"), PathToString<tchar>(assetRoot.path), ConvertString(assetRoot.virtualRoot.view()));
            return MakeUnexpected(Failure{});
        }
        auto resolvedAssetRoot = ResolveAbsolutePath(arena, paths.repoRoot, assetRoot.path);
        if(!resolvedAssetRoot){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetBuilder: failed to resolve asset root '{}': {}"), PathToString<tchar>(assetRoot.path), ConvertString(resolvedAssetRoot.error().message()));
            return MakeUnexpected(Failure{});
        }
        paths.assetRoots.emplace_back(Move(*resolvedAssetRoot), assetRoot.virtualRoot);
    }

    auto outputDirectory = ResolveAbsolutePath(arena, paths.repoRoot, options.outputDirectory);
    if(!outputDirectory){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetBuilder: failed to resolve output directory '{}': {}"), PathToString<tchar>(options.outputDirectory), ConvertString(outputDirectory.error().message()));
        return MakeUnexpected(Failure{});
    }
    paths.outputDirectory = Move(*outputDirectory);

    paths.cacheDirectory = paths.repoRoot / s_AssetCacheDirectory;
    const auto created = EnsureDirectories(paths.cacheDirectory);
    if(!created){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetBuilder: failed to create cache directory '{}': {}"), PathToString<tchar>(paths.cacheDirectory), ConvertString(created.error().message()));
        return MakeUnexpected(Failure{});
    }
    return paths;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

